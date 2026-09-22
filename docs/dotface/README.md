# Cadran "Dot" et badges de detection interactifs

Specification figee du cadran "Dot" ajoute comme nouveau choix de face (a cote
d'Analog / Digital), et du comportement interactif des badges de detection.
Cible : LILYGO T-Watch Ultra (ESP32-S3), ecran 502x410. Le mockup de reference
est `dotface_final.svg` (echelle 410x502, portrait, coordonnees exactes).

Ce document capture la conception approuvee et les contraintes techniques
verifiees dans le code avant implementation. Il ne remplace pas le code, il sert
de reference de conception.

## Palette stricte

| Role | Couleur |
| --- | --- |
| Actif | `#FFFFFF` |
| Inactif / repos | `#5C5C5C` (segments vides : `#3A3A3A`) |
| Notifications et detections | `#E02020` |
| Fond | `#0A0A0A` (bordure `#050505`, lisere externe `#1a1a1a`) |

Boitier : squircle (rectangle tres arrondi, `rx=150 ry=170`), pas un cercle.

## Contenu, de haut en bas (coordonnees SVG 410x502)

1. **Rangee de statut (y ~49-67)** : pastille Meshtastic non lus (`x=55..77`),
   LoRa `x=107`, NFC `x=146`, SD `x=190`, Bluetooth `x=222`, WiFi `x=258`,
   Wardriver `x=295`, GPS `x=330`. Reutilise la logique de visibilite existante
   (`update_lora_indicator`, `update_bt_indicator`, `update_wifi_indicator`,
   `update_sd_indicator`, `update_nfc_indicator`, `update_wardriver_indicator`,
   `clock_screen_set_mesh_count`, `clock_screen_set_gps_active`) ; seuls le
   dessin et les couleurs changent.

2. **Indicateur USB (nouveau, y=130)** : deux rangees de 8 points, gauche
   `x=55..153`, droite `x=247..345`, pas 14px, `r=3`. Au repos : points gris
   statiques, aucun label. Des qu'une presence USB est detectee : les points
   animent en boucle blanc -> rouge -> blanc, cascade ~0.08s par point (vague).
   Label central : eclair (charge) a `x=200,y=127`, texte "DATA" (donnees) a
   `x=200,y=134`, les deux ensemble = eclair `x=178` + "DATA" `x=208` (groupe
   centre). Voir la note "USB : etats detectables" ci-dessous.

3. **Heure en points 5x7 (y=190..274, r=5.2)** : grille custom (voir
   `clock_dot_font`), pas Ndot. Pitch cellule 14px. D1 `x=50`, D2 `x=119.44`,
   deux-points `x=197.84` (r=4.42, y=218 et y=246), D3 `x=220.24`,
   D4 `x=289.68`. Rendu par canvas LVGL (vrais points ronds).

4. **Ligne d'accent (y=300)** : trait rouge plein `x=50 w=245 h=3` opacite 0.9.
   La variante "progression de pas" est reportee (voir note "Pas" ci-dessous).

5. **Date (y=338)** : reutilise `date_label`, `x=50`, `#9a9a9a`.

6. **Badges de detection (y=365-384)** : Flock `x=62 w=40`, EvilT `x=124 w=40`,
   AirT `x=186 w=34`, Flip `x=242 w=34`, Skim `x=298 w=38`. Toujours visibles,
   interactifs (voir section dediee).

7. **Bas (y ~427-446)** : chrono `cx=63`, minuteur `cx=89`, alarme (cloche)
   `x=118`, batterie 13 segments `w=11 h=16` pitch 14 debut `x=140`, pourcentage
   ancre a droite `x=350.88`. Memes logiques que le cadran existant
   (`build_clock_icon`, `layout_battery_indicators`, `*_is_running()`,
   `alarm_is_enabled()`), recolore blanc / gris `#3A3A3A`.

## Badges de detection interactifs

Trois etats visuels par badge :

1. Gris `#5C5C5C` : detecteur desactive.
2. Blanc : detecteur active, aucune detection (pas de chiffre).
3. Rouge `#E02020` (pilule + texte + compteur) : detecteur active et `count > 0`.

Un tap sur un badge (sur `clock_screen`) bascule le detecteur. Il pilote la
**meme** API que les tuiles Tools, donc les deux restent synchronises :

| Badge | API standalone (dans Tools et sur le cadran) |
| --- | --- |
| Flock | `flock_start()` / `flock_stop()` / `flock_is_running()` |
| EvilT | `evil_twin_start()` / `evil_twin_stop()` / `evil_twin_is_running()` |
| AirT | `airtag_start()` / `airtag_stop()` / `airtag_is_running()` |
| Flip | `flipper_start()` / `flipper_stop()` / `flipper_is_running()` |
| Skim | `skimmer_start()` / `skimmer_stop()` / `skimmer_is_running()` |

Compteurs : `*_get_count()`. Le tap reutilise le patron
`lv_obj_add_event_cb(badge, cb, LV_EVENT_CLICKED, ...)`.

Comme les badges sont desormais toujours affiches a positions fixes, l'empilage
dynamique de `update_scan_indicators()` (specifique au cadran Dot) est
simplifie : plus de repositionnement, seulement couleur / compteur / visibilite
du chiffre.

EvilT possede bien un interrupteur independant (`evil_twin_*`), separe du
Wardriver complet : un tap sur EvilT n'active que le detecteur evil-twin.

Conflit radio : demarrer un detecteur BLE quand le WiFi tourne (ou l'inverse)
echoue ; `*_start()` renvoie false, le badge reste gris (meme logique que
`show_radio_conflict_dialog` dans Tools).

## Notes techniques verifiees dans le code

### USB : etats detectables

`charge_state.h` / AXP2101 exposent `isVbusIn()` (presence USB) et `isCharging()`
(courant dans la cellule). TinyUSB (CDC+MSC) n'est demarre qu'a l'activation du
mode USB-SD (`usb_sd.cpp` : `USB.begin()`), pas au boot, et aucun
`ARDUINO_USB_MODE` / `CDC_ON_BOOT` n'est force. Il n'existe donc pas de detection
generique d'une connexion USB-CDC de donnees au simple branchement PC.

Signaux fiables retenus : `isVbusIn()`, `isCharging()`, et `usb_sd_is_running()`
(mode USB-SD/MSC = transfert de donnees actif). Les 4 combinaisons de label du
design sont rendues ainsi : eclair = charge en cours, "DATA" =
`usb_sd_is_running()`, les deux = eclair + DATA. La vague anime des qu'il y a
presence USB ou donnees. La detection d'enumeration brute d'un PC (branche mais
rien ne transfere) demanderait d'initialiser la pile USB OTG au boot : chantier
separe.

### Pas (barre de progression)

Aucune source de pas dans le firmware : ANS (Gadgetbridge) et ANCS (iPhone) ne
transportent que des categories de notifications, pas de podometre. La variante
"rail + remplissage proportionnel" est reportee ; le cadran affiche le trait
rouge plein. L'ajouter demanderait un transport BLE dedie (chantier separe).

### Persistance des detecteurs

Les tuiles Tools ne persistent pas l'etat on/off des detecteurs : c'est un etat
runtime (`*_is_running()`), remis a zero au reboot. Les badges du cadran Dot
s'alignent sur ce comportement (pas de nouveau mecanisme de persistance).

### Rendu des chiffres

Canvas LVGL avec cercles pleins par cellule allumee (vrais points ronds),
redessine a chaque changement de minute. Evite ~140 objets LVGL individuels.

## Regles

- Ne pas modifier le cadran Analog / Digital existant.
- Ne pas casser les tuiles Tools ; elles restent synchronisees avec le cadran.
- Reutiliser les `update_*` et la logique d'etat existante partout ou c'est
  raisonnable.
