#ifndef LATTE_MACHINEID_H
#define LATTE_MACHINEID_H

/*
 * ll_machine_id_protected:
 *   Replicates denisbrodbeck/machineid.ProtectedID(appID).
 *
 *   Reads the platform machine UUID, computes
 *     HMAC-SHA256(key = raw_machine_id, msg = app_id)
 *   and writes the lowercase hex result (64 chars + NUL) into out.
 *
 *   out must be at least 65 bytes.
 *   Returns 0 on success, -1 on failure.
 */
int ll_machine_id_protected(const char *app_id, char *out);

#endif /* LATTE_MACHINEID_H */
