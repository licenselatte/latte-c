#ifndef LATTE_MACHINEID_H
#define LATTE_MACHINEID_H

/*
 * ll_machine_id_hash:
 *   The machine_id every LicenseLatte SDK sends:
 *     HMAC-SHA256(key = raw_machine_id, msg = "licenselatte_" + app_id)
 *   as lowercase hex (64 chars + NUL) in out. raw_machine_id is used byte
 *   for byte, with no trimming. This is denisbrodbeck/machineid's
 *   ProtectedID with the "licenselatte_" prefix on the app ID.
 *
 *   out must be at least 65 bytes.
 *   Returns 0 on success, -1 on failure.
 */
int ll_machine_id_hash(const char *raw_machine_id, const char *app_id, char *out);

/*
 * ll_machine_id_protected:
 *   ll_machine_id_hash() applied to the platform machine UUID.
 *
 *   out must be at least 65 bytes.
 *   Returns 0 on success, -1 on failure.
 */
int ll_machine_id_protected(const char *app_id, char *out);

#endif /* LATTE_MACHINEID_H */
