#include "tailnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Tailnet net;
static const char *cli;
static char json[TAILNET_JSON_MAX + 1];
static int refresh(void) {
    char *args[] = {(char *)cli, "status", "--json", NULL};
    memset(&net, 0, sizeof net);
    if (tailnet_capture(args, json, sizeof json) || tailnet_parse(json, strlen(json), &net)) {
        puts("Cannot read Tailscale status. Check the host app/service and permissions.");
        puts("The client may be stopped, unavailable, or using an unsupported response."); return -1;
    }
    printf("\n\033[1;36mTailscale\033[0m  %s  |  %s\n", net.state, net.self);
    puts(" #  Device                       State    Address");
    for (int i = 0; i < net.count; i++)
        printf("%2d  %-28.28s %-8s %s\n", i + 1, net.peers[i].name,
               net.peers[i].online ? "online" : "offline", net.peers[i].address);
    if (!net.count) puts("No peer devices reported. Use login if this host is not signed in.");
    return 0;
}
static void settings(void) {
    char *args[] = {(char *)cli, "debug", "prefs", NULL}; int dns, routes, shields;
    if (tailnet_capture(args, json, sizeof json) || tailnet_prefs(json, strlen(json), &dns, &routes, &shields)) {
        puts("Current settings unavailable with this client or permission level."); return;
    }
    printf("\nHost-wide settings: DNS %s | subnet routes %s | block incoming %s\n",
           dns ? "on" : "off", routes ? "on" : "off", shields ? "on" : "off");
    puts("These affect every host application, not just OS999.");
}
static int line(char *buf, size_t size) {
    if (!fgets(buf, (int)size, stdin)) return 0;
    if (!strchr(buf, '\n') && !feof(stdin)) {
        int c; while ((c = getchar()) != '\n' && c != EOF) {}
        buf[0] = 0; puts("Input too long; nothing changed."); return 1;
    }
    buf[strcspn(buf, "\r\n")] = 0; return 1;
}
static int confirm(const char *description) {
    char answer[32]; printf("%s\nType yes to apply to this host: ", description); fflush(stdout);
    return line(answer, sizeof answer) && !strcmp(answer, "yes");
}
static void change(const char *verb, const char *arg, const char *description) {
    if (!confirm(description)) { puts("Cancelled. No setting changed."); return; }
    char *args[] = {(char *)cli, (char *)verb, (char *)arg, NULL};
    if (!arg) args[2] = NULL;
    int result = tailnet_run(args);
    printf("Tailscale command finished with status %d.\n", result);
    refresh(); settings();
}
static void ssh_peer(const char *number, const char *user) {
    char *end; long n = strtol(number, &end, 10);
    if (!*number || *end || n < 1 || n > net.count || !tailnet_valid_user(user)) {
        puts("Use: ssh DEVICE_NUMBER USERNAME (for example: ssh 2 ian)"); return;
    }
    TailnetPeer *peer = &net.peers[n - 1];
    if (!strcmp(peer->name, net.self)) { puts("Choose a remote device."); return; }
    if (strcmp(net.state, "Running")) { puts("Connect Tailscale and refresh the list first."); return; }
    if (!peer->online) { puts("Device is offline. Refresh before connecting."); return; }
    printf("Opening SSH to %s (%s) as %s. Exit the remote shell to return.\n", peer->name, peer->address, user);
    puts("Host SSH configuration, host-key checks and authentication remain in effect.");
    char *args[] = {"/usr/bin/ssh", "-o", "ConnectTimeout=10", "-l", (char *)user, peer->address, NULL};
    int result = tailnet_run(args); printf("SSH finished with status %d.\n", result);
}
static void help(void) {
    puts("\nlist / refresh          Refresh devices and connection state");
    puts("ssh NUMBER USER         Open the selected device in this terminal");
    puts("settings                Show host connection settings");
    puts("connect / disconnect    Connect or disconnect this host");
    puts("login                   Start Tailscale's browser login flow");
    puts("dns on|off              Accept tailnet DNS configuration");
    puts("routes on|off           Accept advertised subnet routes");
    puts("shields on|off          Block incoming tailnet connections");
    puts("help / quit             Show commands / close this utility");
}
int main(int argc, char **argv) {
    cli = tailnet_cli();
    if (!cli) {
        puts("Tailscale is not installed in a supported host location.");
        puts("Install the official host client: https://tailscale.com/download");
        puts("OS999 uses that client; it does not bundle a VPN daemon or store login keys."); return 2;
    }
    int status_only = argc == 2 && !strcmp(argv[1], "--status");
    int ok = refresh(); if (status_only) return ok ? 1 : 0;
    help(); char input[256];
    for (;;) {
        printf("\n\033[36mtailscale\033[0m > "); fflush(stdout);
        if (!line(input, sizeof input)) break;
        char *save = NULL, *cmd = strtok_r(input, " \t", &save);
        if (!cmd) continue;
        char *arg = strtok_r(NULL, " \t", &save), *user = strtok_r(NULL, " \t", &save);
        if (strtok_r(NULL, " \t", &save)) { puts("Too many arguments."); continue; }
        if (!strcmp(cmd, "ssh")) { ssh_peer(arg ? arg : "", user ? user : ""); continue; }
        if (user) { puts("Too many arguments."); continue; }
        if (!strcmp(cmd, "dns") || !strcmp(cmd, "routes") || !strcmp(cmd, "shields")) {
            if (!arg || (strcmp(arg, "on") && strcmp(arg, "off"))) { puts("Choose on or off."); continue; }
            char flag[64], description[180];
            const char *name = !strcmp(cmd, "dns") ? "accept-dns" : !strcmp(cmd, "routes") ? "accept-routes" : "shields-up";
            snprintf(flag, sizeof flag, "--%s=%s", name, !strcmp(arg, "on") ? "true" : "false");
            snprintf(description, sizeof description, "Change host-wide %s to %s. This can affect other apps and remote access.", cmd, arg);
            change("set", flag, description); continue;
        }
        if (arg) { puts("This command takes no arguments."); continue; }
        if (!strcmp(cmd, "quit")) break;
        if (!strcmp(cmd, "list") || !strcmp(cmd, "refresh")) refresh();
        else if (!strcmp(cmd, "settings")) settings();
        else if (!strcmp(cmd, "help")) help();
        else if (!strcmp(cmd, "connect")) change("up", "--timeout=30s", "Connect this host using its existing Tailscale preferences.");
        else if (!strcmp(cmd, "disconnect")) change("down", NULL, "Disconnect this host from Tailscale. Existing tailnet sessions may be interrupted.");
        else if (!strcmp(cmd, "login")) change("login", "--timeout=2m", "Start the host Tailscale login flow. Complete authentication in your browser.");
        else puts("Unknown command. Type help.");
    }
    return 0;
}
