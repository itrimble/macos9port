#include "runtime_perf.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    FILE *capture = tmpfile();
    assert(capture);
    int saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0);

    RuntimePerf perf;
    runtime_perf_init(&perf, 0, 1000000);
    for (uint64_t frame = 1; frame <= 60; frame++)
        runtime_perf_present(&perf, frame * 16667);
    fflush(stderr);
    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);

    rewind(capture);
    char output[512] = {0};
    assert(fgets(output, sizeof output, capture));
    fclose(capture);
    assert(strstr(output, "fps=60.0"));
    assert(strstr(output, "frame-ms-p50=16.667"));
    assert(strstr(output, "p95=16.667"));
    assert(strstr(output, "p99=16.667"));
    puts("runtime frame metrics: ok");
    return 0;
}
