#include "archive_fs.h"
#include "mac_cpu.h"
#include "ospi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    char target_triple[32];
    uint32_t drm_width;
    uint32_t drm_height;
    uint32_t drm_refresh_hz;
    char drm_fourcc[8];
    uint32_t evdev_caps;
    uint32_t audio_rate_hz;
    uint32_t audio_channels;
    uint32_t audio_period_frames;
} HostPlatformProfile;

enum {
    EVDEV_CAP_KEYBOARD = 1u << 0,
    EVDEV_CAP_MOUSE_REL = 1u << 1,
    EVDEV_CAP_BUTTONS = 1u << 2
};

enum {
    AUDIT_OK = 0,
    AUDIT_ERR_TARGET = -1,
    AUDIT_ERR_DRM_MODE = -2,
    AUDIT_ERR_EVDEV = -3,
    AUDIT_ERR_AUDIO = -4,
    AUDIT_ERR_ENV_LEAK = -5,
    AUDIT_ERR_PATH_ESCAPE = -6,
    AUDIT_ERR_INBOUND_LISTEN = -7
};

static int validate_host_platform_profile(const HostPlatformProfile *p) {
    if (!p) return AUDIT_ERR_TARGET;
    if (strcmp(p->target_triple, "macos-arm64") != 0 &&
        strcmp(p->target_triple, "linux-rpi-arm64") != 0 &&
        strcmp(p->target_triple, "linux-x86_64") != 0) {
        return AUDIT_ERR_TARGET;
    }
    if (p->drm_width < 640u || p->drm_height < 480u || p->drm_refresh_hz < 60u) {
        return AUDIT_ERR_DRM_MODE;
    }
    if (strcmp(p->drm_fourcc, "XR24") != 0 && strcmp(p->drm_fourcc, "AR24") != 0) {
        return AUDIT_ERR_DRM_MODE;
    }
    uint32_t req_ev = EVDEV_CAP_KEYBOARD | EVDEV_CAP_MOUSE_REL | EVDEV_CAP_BUTTONS;
    if ((p->evdev_caps & req_ev) != req_ev) {
        return AUDIT_ERR_EVDEV;
    }
    if (p->audio_rate_hz != 48000u || p->audio_channels != 2u ||
        p->audio_period_frames < 256u || p->audio_period_frames > 1024u) {
        return AUDIT_ERR_AUDIO;
    }
    return AUDIT_OK;
}

static int audit_guest_env_allowlist(const char *const *envp) {
    if (!envp) return AUDIT_OK;
    for (size_t i = 0; envp[i] != NULL; i++) {
        const char *kv = envp[i];
        if (strncmp(kv, "OS999_BRIDGE=", 13) == 0 ||
            strncmp(kv, "OS999_VOLUME=", 13) == 0 ||
            strncmp(kv, "LANG=C", 6) == 0) {
            continue;
        }
        return AUDIT_ERR_ENV_LEAK;
    }
    return AUDIT_OK;
}

static int audit_guest_host_path_request(const char *rel_path) {
    if (!rel_path || rel_path[0] == '\0') {
        return AUDIT_ERR_PATH_ESCAPE;
    }
    if (strstr(rel_path, "/proc/") != NULL ||
        strstr(rel_path, "/dev/mem") != NULL ||
        strstr(rel_path, ".ssh") != NULL) {
        return AUDIT_ERR_PATH_ESCAPE;
    }
    char joined[512];
    if (volume_join(joined, sizeof(joined), "/tmp/vol", rel_path) != 0) {
        return AUDIT_ERR_PATH_ESCAPE;
    }
    return AUDIT_OK;
}

static int audit_guest_socket_bind(const char *bind_addr, uint16_t port, int is_inbound_listen) {
    if (is_inbound_listen) {
        if (!bind_addr || strcmp(bind_addr, "127.0.0.1") != 0 || port < 1024u) {
            return AUDIT_ERR_INBOUND_LISTEN;
        }
    }
    return AUDIT_OK;
}

static void test_multi_host_capability_matrix(void) {
    CHECK(host_ram() > 0u);
    CHECK(host_arch() != NULL && host_arch()[0] != '\0');

    uint32_t full_ev = EVDEV_CAP_KEYBOARD | EVDEV_CAP_MOUSE_REL | EVDEV_CAP_BUTTONS;
    HostPlatformProfile mac = {
        "macos-arm64", 1024u, 768u, 60u, "AR24", full_ev, 48000u, 2u, 512u
    };
    HostPlatformProfile rpi5 = {
        "linux-rpi-arm64", 1280u, 720u, 60u, "XR24", full_ev, 48000u, 2u, 512u
    };
    HostPlatformProfile x86 = {
        "linux-x86_64", 1920u, 1080u, 60u, "XR24", full_ev, 48000u, 2u, 256u
    };
    CHECK(validate_host_platform_profile(&mac) == AUDIT_OK);
    CHECK(validate_host_platform_profile(&rpi5) == AUDIT_OK);
    CHECK(validate_host_platform_profile(&x86) == AUDIT_OK);

    HostPlatformProfile bad_drm = rpi5;
    strcpy(bad_drm.drm_fourcc, "RG16");
    CHECK(validate_host_platform_profile(&bad_drm) == AUDIT_ERR_DRM_MODE);

    HostPlatformProfile bad_evdev = rpi5;
    bad_evdev.evdev_caps = EVDEV_CAP_KEYBOARD;
    CHECK(validate_host_platform_profile(&bad_evdev) == AUDIT_ERR_EVDEV);

    HostPlatformProfile bad_audio = rpi5;
    bad_audio.audio_period_frames = 64u;
    CHECK(validate_host_platform_profile(&bad_audio) == AUDIT_ERR_AUDIO);
}

static void test_guest_cpu_memory_isolation(void) {
    size_t ram_sz = 64u * 1024u;
    MacCpu *cpu = mac_cpu_create_model(ram_sz, MAC_CPU_68000);
    CHECK(cpu != NULL);
    CHECK(mac_cpu_ram_size(cpu) == ram_sz);

    /* Valid in-bounds big-endian read/write succeeds without halting. */
    CHECK(mac_cpu_write(cpu, 0x1000u, 4u, 0xCAFEBABEu) == 0);
    CHECK(!mac_cpu_halted(cpu));
    CHECK(mac_cpu_read(cpu, 0x1000u, 4u) == 0xCAFEBABEu);

    /* Out-of-bounds write at ram_sz must fail and halt the guest CPU. */
    CHECK(mac_cpu_write(cpu, (uint32_t)ram_sz, 4u, 0xDEADBEEFu) != 0);
    CHECK(mac_cpu_halted(cpu));
    mac_cpu_destroy(cpu);

    /* Out-of-bounds read at 0xFFFFFFFF must return 0 and halt the guest CPU. */
    cpu = mac_cpu_create_model(ram_sz, MAC_CPU_68020);
    CHECK(cpu != NULL);
    CHECK(mac_cpu_read(cpu, 0xFFFFFFFFu, 4u) == 0u);
    CHECK(mac_cpu_halted(cpu));
    mac_cpu_destroy(cpu);
}

static void test_sandbox_env_path_and_network_isolation(void) {
    const char *clean_env[] = {
        "OS999_BRIDGE=/os999-shm-1",
        "OS999_VOLUME=/tmp/vol",
        "LANG=C",
        NULL
    };
    CHECK(audit_guest_env_allowlist(clean_env) == AUDIT_OK);

    const char *leaked_aws[] = {
        "OS999_BRIDGE=/os999-shm-1",
        "AWS_SECRET_ACCESS_KEY=AKIAIOSFODNN7EXAMPLE",
        NULL
    };
    CHECK(audit_guest_env_allowlist(leaked_aws) == AUDIT_ERR_ENV_LEAK);

    const char *leaked_ssh[] = {
        "SSH_AUTH_SOCK=/private/tmp/com.apple.launchd/Listeners",
        NULL
    };
    CHECK(audit_guest_env_allowlist(leaked_ssh) == AUDIT_ERR_ENV_LEAK);

    CHECK(audit_guest_host_path_request("System Folder/Preferences/ GamePrefs") == AUDIT_OK);
    CHECK(audit_guest_host_path_request("../../etc/passwd") == AUDIT_ERR_PATH_ESCAPE);
    CHECK(audit_guest_host_path_request("/proc/self/mem") == AUDIT_ERR_PATH_ESCAPE);
    CHECK(audit_guest_host_path_request("/dev/mem") == AUDIT_ERR_PATH_ESCAPE);
    CHECK(audit_guest_host_path_request("Users/ian/.ssh/id_ed25519") == AUDIT_ERR_PATH_ESCAPE);

    CHECK(audit_guest_socket_bind("0.0.0.0", 8080u, 1) == AUDIT_ERR_INBOUND_LISTEN);
    CHECK(audit_guest_socket_bind("127.0.0.1", 80u, 1) == AUDIT_ERR_INBOUND_LISTEN);
    CHECK(audit_guest_socket_bind("127.0.0.1", 19999u, 1) == AUDIT_OK);
}

int main(void) {
    test_multi_host_capability_matrix();
    test_guest_cpu_memory_isolation();
    test_sandbox_env_path_and_network_isolation();
    if (failures > 0) {
        fprintf(stderr, "test_platform_sandbox_audit: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_platform_sandbox_audit: all %d checks passed\n", checks);
    return 0;
}
