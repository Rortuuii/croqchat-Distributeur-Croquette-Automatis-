/* Commande des quatre entrees du moteur pas a pas. */

#include "motor_controller.h"
#include "croqchat_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const int broches[4] = {
    CROQCHAT_GPIO_MOTOR_IN1, CROQCHAT_GPIO_MOTOR_IN2,
    CROQCHAT_GPIO_MOTOR_IN3, CROQCHAT_GPIO_MOTOR_IN4
};

// Séquence des 8 demi-pas
static const int sequence[8][4] = {
    {1,0,0,0}, {1,1,0,0},
    {0,1,0,0}, {0,1,1,0},
    {0,0,1,0}, {0,0,1,1},
    {0,0,0,1}, {1,0,0,1}
};

static bool initialise = false;
static bool en_marche = false;
static int pas = 0;
static int64_t prochain_pas = 0;

/* Avance les demi-pas a la vitesse demandee sans bloquer la boucle. */
void motor_controller_update(bool tourne, uint32_t vitesse)
{
    // Initialisation une seule fois
    if (!initialise) {
        for (int indice = 0; indice < 4; indice++) {
            gpio_reset_pin(broches[indice]);
            gpio_set_level(broches[indice], 0);
            gpio_set_direction(broches[indice], GPIO_MODE_OUTPUT);
        }
        initialise = true;
    }

    // Arrêt du moteur
    if (!tourne || vitesse == 0) {
        motor_controller_stop();
        return;
    }

    // Gestion du temps
    int64_t instant_actuel = esp_timer_get_time();

    if (!en_marche)
        prochain_pas = instant_actuel;

    en_marche = true;

    if (instant_actuel < prochain_pas)
        return;

    // Effectuer un demi-pas
    for (int indice = 0; indice < 4; indice++)
        gpio_set_level(broches[indice], sequence[pas][indice]);

    pas = (pas + 7) % 8; /* Parcourir les demi-pas dans le sens inverse. */
    /* Vitesse en demi-pas par seconde, limitee a la cadence initiale. */
    if (vitesse > 500) vitesse = 500;
    prochain_pas = instant_actuel + 1000000 / vitesse;
}

/* Coupe les quatre sorties du moteur. */
void motor_controller_stop(void)
{
    en_marche = false;

    for (int indice = 0; indice < 4; indice++)
        gpio_set_level(broches[indice], 0);
}

/* Indique si une rotation est commandee. */
bool motor_controller_is_running(void)
{
    return en_marche;
}
