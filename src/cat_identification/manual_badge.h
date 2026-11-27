/* Commande manuelle du moteur par badge, avec tare optionnelle avant demarrage. */
#ifndef MANUAL_BADGE_H
#define MANUAL_BADGE_H
#include <stdbool.h>
/* Active le mode manuel pour toute carte detectee, sans filtrage UID. */
bool manual_badge_init(bool activer);
/* disponible=false : une distribution, une tare ou un defaut interdit le mode badge.
 * STOP verrouille le mode jusqu'au retrait du badge. Ne touche pas un moteur FB03. */
void manual_badge_update(bool disponible, bool arret_demande, bool tare_avant_demarrage);
bool manual_badge_is_active(void);
bool manual_badge_is_taring(void);
#endif
