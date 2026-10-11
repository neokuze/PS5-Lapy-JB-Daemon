/* Lapy HDD diagnostic — read-only.
 * Reports pending update files, mounts and storage devices. Changes nothing
 * on the system; the only write is the report at /data/lapy_diag.txt.
 * Output: notification summary, /data/lapy_diag.txt, and TCP port 9030
 * (serves the report to the first client, waits up to 5 minutes). */
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ps5/kernel.h>

int sceNotificationSend(int userId, int isLogged, const char *payload);

#define DIAG_PORT 9030
#define REPORT_PATH "/data/lapy_diag.txt"

static char report[64 * 1024];
static size_t rlen;

static void out(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(report + rlen, sizeof(report) - rlen, fmt, ap);
    va_end(ap);
    if (n > 0) rlen += ((size_t)n < sizeof(report) - rlen) ? (size_t)n : sizeof(report) - rlen - 1;
}

static void notif(const char *text) {
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\","
        "\"channelType\":\"Downloads\",\"useCaseId\":\"IDC\","
        "\"toastOverwriteType\":\"Yes\",\"isImmediate\":true,\"priority\":100,"
        "\"viewData\":{\"icon\":{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"download\"}},"
        "\"message\":{\"body\":\"%s\"},\"subMessage\":{\"body\":\"Lapy HDD diag\"}}},"
        "\"localNotificationId\":\"LAPY_DIAG\"}", text);
    sceNotificationSend(0xFE, 1, json);
}

static int check_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) { out("  %-40s (no existe)\n", path); return 0; }
    out("  %-40s %lld bytes\n", path, (long long)st.st_size);
    return 1;
}

static void list_dir(const char *path, const char *filter) {
    DIR *d = opendir(path);
    if (!d) { out("  (no se puede abrir %s)\n", path); return; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (filter && !strstr(filter, "*") ) {
            /* filter = comma-free list of prefixes separated by '|' */
            char f[128]; strncpy(f, filter, sizeof(f) - 1); f[sizeof(f) - 1] = 0;
            int hit = 0;
            for (char *tok = strtok(f, "|"); tok; tok = strtok(NULL, "|"))
                if (strncmp(de->d_name, tok, strlen(tok)) == 0) hit = 1;
            if (!hit) continue;
        }
        out("  %s/%s\n", path, de->d_name);
    }
    closedir(d);
}

static int list_mounts(void) {
    static struct statfs fs[128];
    int n = getfsstat(fs, sizeof(fs), MNT_NOWAIT);
    int ext = 0;
    if (n < 0) { out("  getfsstat fallo\n"); return 0; }
    for (int i = 0; i < n; i++) {
        out("  %-28s <- %-28s [%s]\n", fs[i].f_mntonname, fs[i].f_mntfromname, fs[i].f_fstypename);
        if (strncmp(fs[i].f_mntonname, "/mnt/ext", 8) == 0) ext = 1;
    }
    return ext;
}

static void serve_report(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(DIAG_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) == 0 && listen(s, 1) == 0) {
        fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
        struct timeval tv = { 300, 0 };
        if (select(s + 1, &rf, NULL, NULL, &tv) > 0) {
            int c = accept(s, NULL, NULL);
            if (c >= 0) { write(c, report, rlen); close(c); }
        }
    }
    close(s);
}

int main(void) {
    uint32_t fw = kernel_get_fw_version();
    out("=== Lapy HDD diag ===\nfirmware: 0x%08x (%x.%02x)\n\n", fw, fw >> 24, (fw >> 16) & 0xff);

    out("[update pendiente]\n");
    int pup = check_file("/update/PS5UPDATE.PUP");
    pup |= check_file("/update/PS5UPDATE.PUP.net.temp");
    list_dir("/update", NULL);

    out("\n[montajes]\n");
    int ext = list_mounts();

    out("\n[/mnt]\n");
    list_dir("/mnt", NULL);

    out("\n[/dev: almacenamiento]\n");
    list_dir("/dev", "da|ssd|usb|ext|lvd|nvme|sbram|md");

    out("\n[resumen] PUP pendiente: %s | /mnt/ext montado: %s\n",
        pup ? "SI" : "no", ext ? "SI" : "no");

    int fd = open(REPORT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, report, rlen); close(fd); }

    char msg[160];
    snprintf(msg, sizeof(msg), "PUP pendiente: %s / ext montado: %s\\nReporte: %s y TCP %d",
             pup ? "SI" : "no", ext ? "SI" : "no", REPORT_PATH, DIAG_PORT);
    notif(msg);

    serve_report();
    return 0;
}
