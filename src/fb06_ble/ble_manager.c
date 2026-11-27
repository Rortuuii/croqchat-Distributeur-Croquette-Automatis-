/* Connexion Bluetooth et transport des messages. */
#include "ble_manager.h"
#include "croqchat_config.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *ETIQUETTE_JOURNAL = "FB06";
typedef struct {
    uint32_t session;
    uint16_t longueur;
    bool perdu;
    uint8_t donnees[CROQCHAT_FRAGMENT_MAX];
} rx_fragment_t;
typedef struct {
    uint32_t session;
    char texte[CROQCHAT_TX_LINE_MAX];
} tx_line_t;
typedef struct {
    uint32_t session;
    uint16_t identifiant_connexion;
    bool notifications_activees;
} connection_t;
static connection_t connexion = {.identifiant_connexion = BLE_HS_CONN_HANDLE_NONE};
static portMUX_TYPE verrou_etat = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t file_reception;
static QueueHandle_t file_emission;
static ble_command_handler_t gestionnaire_commandes;
static uint16_t identifiant_valeur_emission;
static uint8_t type_adresse_locale;
static struct ble_npl_callout minuterie_emission;
static tx_line_t emission_en_attente;
static size_t position_emission;
static uint32_t session_emission;
static protocol_stream_t flux;
static int gap_event(struct ble_gap_event *evenement, void *argument);

/* NimBLE encode les UUID 128 bits en ordre little-endian. */
static const ble_uuid128_t uuid_service = BLE_UUID128_INIT(
    0x01,0x00,0x00,0x00,0x89,0x5a,0x4b,0xbb,0x30,0x4b,0xd0,0xfd,0x10,0x4c,0xa4,0x32);
static const ble_uuid128_t uuid_reception = BLE_UUID128_INIT(
    0x02,0x00,0x00,0x00,0x89,0x5a,0x4b,0xbb,0x30,0x4b,0xd0,0xfd,0x10,0x4c,0xa4,0x32);
static const ble_uuid128_t uuid_emission = BLE_UUID128_INIT(
    0x03,0x00,0x00,0x00,0x89,0x5a,0x4b,0xbb,0x30,0x4b,0xd0,0xfd,0x10,0x4c,0xa4,0x32);

/* Copie les informations de connexion protegees contre les acces simultanes. */
/* Copie atomiquement l’état de la session entre les tâches BLE et la boucle applicative. */
static connection_t connection_snapshot(void)
{
    portENTER_CRITICAL(&verrou_etat);
    connection_t resultat = connexion;
    portEXIT_CRITICAL(&verrou_etat);
    return resultat;
}
/* Verifie que la session appartient encore a la connexion courante. */
bool ble_manager_session_active(uint32_t session)
{
    connection_t etat = connection_snapshot();
    return etat.identifiant_connexion != BLE_HS_CONN_HANDLE_NONE && etat.session == session;
}
/* Place une ligne de reponse dans la file d emission. */
/* Place une réponse complète dans la file d’émission de la session qui a envoyé la commande. */
esp_err_t ble_manager_send_line(uint32_t session, const char *ligne)
{
    if (!ligne || !file_emission) return ESP_ERR_INVALID_ARG;
    connection_t etat = connection_snapshot();
    if (etat.session != session || etat.identifiant_connexion == BLE_HS_CONN_HANDLE_NONE || !etat.notifications_activees) {
        ESP_LOGW(ETIQUETTE_JOURNAL, "[TX] Non envoyee : client absent ou notifications desactivees");
        return ESP_ERR_INVALID_STATE;
    }
    size_t longueur = strlen(ligne);
    while (longueur && (ligne[longueur - 1] == '\r' || ligne[longueur - 1] == '\n')) --longueur;
    if (longueur + 2 > CROQCHAT_TX_LINE_MAX) return ESP_ERR_INVALID_SIZE;
    tx_line_t message = {.session = session};
    memcpy(message.texte, ligne, longueur);
    message.texte[longueur] = '\n';
    message.texte[longueur + 1] = '\0';
    if (xQueueSend(file_emission, &message, 0) != pdTRUE) {
        ESP_LOGE(ETIQUETTE_JOURNAL, "[TX] File pleine : reponse abandonnee");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(ETIQUETTE_JOURNAL, "[TX] En file : %.*s", (int)longueur, ligne);
    return ESP_OK;
}
/* Les notifications s'executent dans la tache NimBLE, comme les evenements GAP.
 * Une deconnexion ne peut donc pas intercaler une nouvelle session dans un envoi. */
/* Découpe les lignes en notifications BLE et reprend l’envoi quand la pile radio est disponible. */
static void tx_tick(struct ble_npl_event *evenement)
{
    (void)evenement;
    connection_t etat = connection_snapshot();
    if (session_emission != etat.session || !etat.notifications_activees) {
        emission_en_attente.texte[0] = '\0';
        position_emission = 0;
        session_emission = etat.session;
    }
    if (etat.identifiant_connexion != BLE_HS_CONN_HANDLE_NONE && etat.notifications_activees) {
        if (!emission_en_attente.texte[0]) {
            // Travail borne par tick pour laisser progresser les evenements GAP/RX.
            for (unsigned indice = 0; indice < CROQCHAT_TX_QUEUE_DEPTH; ++indice) {
                if (xQueueReceive(file_emission, &emission_en_attente, 0) != pdTRUE) break;
                if (emission_en_attente.session == etat.session) break;
                emission_en_attente.texte[0] = '\0';
            }
            position_emission = 0;
        }
        if (emission_en_attente.texte[0]) {
            size_t restant = strlen(emission_en_attente.texte + position_emission);
            uint16_t nombre = restant > CROQCHAT_NOTIFY_MAX ? CROQCHAT_NOTIFY_MAX : restant;
            struct os_mbuf *paquet = ble_hs_mbuf_from_flat(emission_en_attente.texte + position_emission, nombre);
            if (paquet) {
                // notify_custom prend possession du mbuf, y compris en cas d'echec.
                int code_retour = ble_gatts_notify_custom(etat.identifiant_connexion, identifiant_valeur_emission, paquet);
                if (code_retour == 0) {
                    position_emission += nombre;
                    if (!emission_en_attente.texte[position_emission]) emission_en_attente.texte[0] = '\0';
                } else {
                    ESP_LOGW(ETIQUETTE_JOURNAL, "[TX] Notification differee, rc=%d", code_retour);
                }
            }
        }
    } else {
        // Ne jamais reutiliser des reponses d'une ancienne connexion.
        xQueueReset(file_emission);
    }
    ble_npl_callout_reset(&minuterie_emission, ble_npl_time_ms_to_ticks32(25));
}
/* Rend le distributeur visible pour une connexion Bluetooth. */
/* Publie le service et le nom CroqChat pour la découverte par le téléphone. */
static void advertise(void)
{
    struct ble_hs_adv_fields champs = {0};
    champs.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    champs.uuids128 = (ble_uuid128_t *)&uuid_service;
    champs.num_uuids128 = 1;
    champs.uuids128_is_complete = 1;
    int code_retour = ble_gap_adv_set_fields(&champs);
    if (code_retour) { ESP_LOGE(ETIQUETTE_JOURNAL, "Publicite UUID impossible : %d", code_retour); return; }
    struct ble_hs_adv_fields reponse_annonce = {0};
    reponse_annonce.name = (const uint8_t *)CROQCHAT_DEVICE_NAME;
    reponse_annonce.name_len = strlen(CROQCHAT_DEVICE_NAME);
    reponse_annonce.name_is_complete = 1;
    code_retour = ble_gap_adv_rsp_set_fields(&reponse_annonce);
    if (code_retour) { ESP_LOGE(ETIQUETTE_JOURNAL, "Annonce du nom impossible : %d", code_retour); return; }
    struct ble_gap_adv_params parametres = {0};
    parametres.conn_mode = BLE_GAP_CONN_MODE_UND;
    parametres.disc_mode = BLE_GAP_DISC_MODE_GEN;
    code_retour = ble_gap_adv_start(type_adresse_locale, NULL, BLE_HS_FOREVER, &parametres, gap_event, NULL);
    if (code_retour) ESP_LOGE(ETIQUETTE_JOURNAL, "Demarrage publicite impossible : %d", code_retour);
    else ESP_LOGI(ETIQUETTE_JOURNAL, "Publicite activee / relancee ; attente de connexion");
}
/* Met a jour la connexion et son numero de session. */
/* Attribue une nouvelle session pour invalider les messages de l’ancienne connexion. */
static void set_connection(uint16_t identifiant_connexion)
{
    portENTER_CRITICAL(&verrou_etat);
    ++connexion.session;
    connexion.identifiant_connexion = identifiant_connexion;
    connexion.notifications_activees = false;
    portEXIT_CRITICAL(&verrou_etat);
    emission_en_attente.texte[0] = '\0';
    xQueueReset(file_reception);
    xQueueReset(file_emission);
}
/* Traite les evenements de connexion et de deconnexion Bluetooth. */
static int gap_event(struct ble_gap_event *evenement, void *argument)
{
    (void)argument;
    switch (evenement->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (!evenement->connect.status) {
                set_connection(evenement->connect.conn_handle);
                ESP_LOGI(ETIQUETTE_JOURNAL, "Telephone connecte");
            } else {
                ESP_LOGW(ETIQUETTE_JOURNAL, "Connexion echouee : %d", evenement->connect.status);
                advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            set_connection(BLE_HS_CONN_HANDLE_NONE);
            ESP_LOGI(ETIQUETTE_JOURNAL, "Telephone deconnecte : raison=%d", evenement->disconnect.reason);
            advertise();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (evenement->subscribe.attr_handle == identifiant_valeur_emission) {
                portENTER_CRITICAL(&verrou_etat);
                connexion.notifications_activees = evenement->subscribe.cur_notify != 0;
                portEXIT_CRITICAL(&verrou_etat);
                if (!evenement->subscribe.cur_notify) {
                    emission_en_attente.texte[0] = '\0';
                    xQueueReset(file_emission);
                }
                ESP_LOGI(ETIQUETTE_JOURNAL, "Notifications TX : %s", evenement->subscribe.cur_notify ? "ON" : "OFF");
            }
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
        default: break;
    }
    return 0;
}
/* Place les octets envoyes par l application dans la file de reception. */
/* Le callback GATT place les écritures RX dans une file pour laisser le traitement à la tâche de réception. */
static int gatt_access(uint16_t identifiant_connexion, uint16_t identifiant_attribut,
                       struct ble_gatt_access_ctxt *contexte, void *argument)
{
    (void)identifiant_connexion; (void)identifiant_attribut; (void)argument;
    if (contexte->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    rx_fragment_t fragment = {.session = connection_snapshot().session};
    size_t longueur = OS_MBUF_PKTLEN(contexte->om);
    if (longueur > sizeof(fragment.donnees)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (ble_hs_mbuf_to_flat(contexte->om, fragment.donnees, sizeof(fragment.donnees), &fragment.longueur)) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!fragment.longueur) return 0;
    if (xQueueSend(file_reception, &fragment, 0) != pdTRUE) {
        // Perte de trame : marqueur ordonne avant les futurs fragments, resynchronisation sur LF.
        xQueueReset(file_reception);
        rx_fragment_t perdu = {.session = fragment.session, .perdu = true};
        xQueueSend(file_reception, &perdu, 0);
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &uuid_service.u,
     .characteristics = (struct ble_gatt_chr_def[]) {
         {.uuid = &uuid_reception.u, .access_cb = gatt_access,
          .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
         {.uuid = &uuid_emission.u, .access_cb = gatt_access,
          .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &identifiant_valeur_emission},
         {0}
     }},
    {0}
};
/* Transforme une erreur de reception en reponse pour l application. */
static void stream_error(const char *code, void *contexte)
{
    const uint32_t session = *(uint32_t *)contexte;
    ESP_LOGW(ETIQUETTE_JOURNAL, "[RX] Erreur : %s ; resynchronisation sur LF si trame perdue", code);
    char reponse[64];
    snprintf(reponse, sizeof(reponse), "ERR;code=%s", code);
    ble_manager_send_line(session, reponse);
}
/* Verifie une ligne complete et transmet la commande au gestionnaire. */
static void received_line(const char *ligne, void *contexte)
{
    uint32_t session = *(uint32_t *)contexte;
    ESP_LOGI(ETIQUETTE_JOURNAL, "[RX] Commande : %s", ligne);
    ble_command_event_t evenement = {.session = session};
    const char *erreur;
    if (!protocol_parse(ligne, &evenement.commande, &erreur)) {
        stream_error(erreur, &session);
    } else if (ble_manager_session_active(session)) {
        if (!gestionnaire_commandes(&evenement)) stream_error("COMMAND_QUEUE_FULL", &session);
    }
}
/* Assemble les fragments recus en lignes completes dans une tache dediee. */
/* Reconstitue les lignes à partir des fragments de la session courante. */
static void rx_task(void *argument)
{
    (void)argument;
    rx_fragment_t fragment;
    uint32_t session_flux = 0;
    for (;;) {
        if (xQueueReceive(file_reception, &fragment, portMAX_DELAY) != pdTRUE) continue;
        if (!ble_manager_session_active(fragment.session)) continue;
        if (session_flux != fragment.session) {
            protocol_stream_reset(&flux);
            session_flux = fragment.session;
        }
        if (fragment.perdu) {
            protocol_stream_reset(&flux);
            flux.abandon_en_cours = true;
            stream_error("RX_QUEUE_FULL", &session_flux);
            continue;
        }
        protocol_stream_feed(&flux, fragment.donnees, fragment.longueur, received_line, stream_error, &session_flux);
    }
}
/* Lance l annonce Bluetooth lorsque la pile est prete. */
/* Attend que NimBLE soit prêt avant de choisir l’adresse locale et d’annoncer le service. */
static void on_sync(void)
{
    int code_retour = ble_hs_util_ensure_addr(0);
    if (!code_retour) code_retour = ble_hs_id_infer_auto(0, &type_adresse_locale);
    if (code_retour) { ESP_LOGE(ETIQUETTE_JOURNAL, "Adresse BLE indisponible : %d", code_retour); return; }
    advertise();
    ble_npl_callout_reset(&minuterie_emission, ble_npl_time_ms_to_ticks32(25));
}
/* Invalide la connexion lorsque la pile Bluetooth redemarre. */
static void on_reset(int raison)
{
    set_connection(BLE_HS_CONN_HANDLE_NONE);
    ESP_LOGE(ETIQUETTE_JOURNAL, "Reinitialisation de la pile BLE : %d", raison);
}
/* Execute la pile Bluetooth dans sa tache dediee. */
static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
/* Configure le service Bluetooth, ses files et ses taches. */
esp_err_t ble_manager_init(ble_command_handler_t gestionnaire)
{
    if (!gestionnaire) return ESP_ERR_INVALID_ARG;
    gestionnaire_commandes = gestionnaire;
    file_reception = xQueueCreate(CROQCHAT_RX_QUEUE_DEPTH, sizeof(rx_fragment_t));
    file_emission = xQueueCreate(CROQCHAT_TX_QUEUE_DEPTH, sizeof(tx_line_t));
    if (!file_reception || !file_emission) return ESP_ERR_NO_MEM;
    esp_err_t erreur = nimble_port_init();
    if (erreur != ESP_OK) return erreur;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int code_retour = ble_svc_gap_device_name_set(CROQCHAT_DEVICE_NAME);
    if (!code_retour) code_retour = ble_gatts_count_cfg(services);
    if (!code_retour) code_retour = ble_gatts_add_svcs(services);
    if (code_retour) { ESP_LOGE(ETIQUETTE_JOURNAL, "Configuration GATT impossible : %d", code_retour); return ESP_FAIL; }
    ble_npl_callout_init(&minuterie_emission, nimble_port_get_dflt_eventq(), tx_tick, NULL);
    if (xTaskCreate(rx_task, "croqchat_rx", 8192, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(ETIQUETTE_JOURNAL, "Nom : %s", CROQCHAT_DEVICE_NAME);
    ESP_LOGI(ETIQUETTE_JOURNAL, "Service : %s", CROQCHAT_SERVICE_UUID);
    ESP_LOGI(ETIQUETTE_JOURNAL, "RX WRITE : %s", CROQCHAT_RX_UUID);
    ESP_LOGI(ETIQUETTE_JOURNAL, "TX NOTIFY : %s", CROQCHAT_TX_UUID);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
