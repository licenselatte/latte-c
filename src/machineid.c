#include "machineid.h"
#include <ctype.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Platform machine ID retrieval --------------------------------------- */

#if defined(__APPLE__)

/*
 * Replicate machineid's darwin extractID:
 *   Find line with "IOPlatformUUID", split after `" = "`, trim trailing `"`.
 * Then apply trim() = TrimSpace(Trim(s, "\n")).
 */
static int get_raw_machine_id(char *out, size_t out_size) {
  FILE *fp = popen("ioreg -rd1 -c IOPlatformExpertDevice", "r");
  if (!fp)
    return -1;

  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), fp)) {
    if (!strstr(line, "IOPlatformUUID"))
      continue;

    /* Find `" = "` separator (matches Go SplitAfter logic) */
    const char *sep = "\" = \"";
    char *pos = strstr(line, sep);
    if (!pos)
      continue;

    /* Everything after the separator */
    char *value = pos + strlen(sep);

    /* ioreg returns
     * `"IOPlatformUUID" = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"\n` */

    /* strip trailing \n */
    value[strcspn(value, "\n")] = '\0';
    size_t vlen = strlen(value);

    /* strip trailing " */
    if (vlen > 0 && value[vlen - 1] == '"')
      value[--vlen] = '\0';

    /* no leading whitespace or ", sep already does that */

    if (vlen == 0 || vlen >= out_size)
      continue;
    memcpy(out, value, vlen + 1);
    found = 1;
    break;
  }
  pclose(fp);
  return found ? 0 : -1;
}

#elif defined(__linux__)

static int get_raw_machine_id(char *out, size_t out_size) {
  static const char *paths[] = {"/var/lib/dbus/machine-id", "/etc/machine-id",
                                NULL};
  for (int i = 0; paths[i]; i++) {
    FILE *fp = fopen(paths[i], "r");
    if (!fp)
      continue;
    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (n == 0)
      continue;

    buf[n] = '\0';
    if (n > 0 && buf[n - 1] == '\n')
      buf[--n] = '\0';
    if (n == 0 || n >= out_size)
      continue;
    memcpy(out, buf, n + 1);

    return 0;
  }
  return -1;
}

#elif defined(_WIN32)

#include <windows.h>

static int get_raw_machine_id(char *out, size_t out_size) {
  HKEY hkey;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0,
                    KEY_READ | KEY_WOW64_64KEY, &hkey) != ERROR_SUCCESS)
    return -1;

  char buf[256];
  DWORD buf_size = sizeof(buf);
  DWORD type;
  LONG r = RegQueryValueExA(hkey, "MachineGuid", NULL, &type, (LPBYTE)buf,
                            &buf_size);
  RegCloseKey(hkey);
  if (r != ERROR_SUCCESS || type != REG_SZ)
    return -1;
  /* buf_size includes the null terminator for REG_SZ */
  size_t len = buf_size - 1;
  if (len == 0 || len >= out_size)
    return -1;
  memcpy(out, buf, len + 1);
  return 0;
}

#else
#error "Unsupported platform: add a get_raw_machine_id() implementation"
#endif

/* --- HMAC-SHA256 protect ------------------------------------------------- */

/*
 * protect(appID, id) = HMAC-SHA256(key=id_bytes, msg=appID_bytes), hex-encoded.
 * Must match denisbrodbeck/machineid protect() exactly.
 */
static int protect(const char *app_id, const char *machine_id, char *out) {
  unsigned char mac[crypto_auth_hmacsha256_BYTES];
  crypto_auth_hmacsha256_state state;

  if (crypto_auth_hmacsha256_init(&state, (const unsigned char *)machine_id,
                                  strlen(machine_id)) != 0)
    return -1;

  if (crypto_auth_hmacsha256_update(&state, (const unsigned char *)app_id,
                                    strlen(app_id)) != 0)
    return -1;

  if (crypto_auth_hmacsha256_final(&state, mac) != 0)
    return -1;

  for (int i = 0; i < (int)crypto_auth_hmacsha256_BYTES; i++)
    snprintf(out + 2 * i, 3, "%02x", mac[i]);
  out[64] = '\0';
  return 0;
}

/* --- Public API ---------------------------------------------------------- */

int ll_machine_id_protected(const char *app_id, char *out) {
  char raw[512];
  if (get_raw_machine_id(raw, sizeof(raw)) < 0)
    return -1;
  return protect(app_id, raw, out);
}
