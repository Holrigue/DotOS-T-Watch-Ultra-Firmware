# Phase 2.2 : données santé depuis le tracker

Objectif : afficher sur la T-Watch quatre métriques mesurées par l'Amazfit
Helio de l'utilisateur, synchronisées par Gadgetbridge sur le téléphone.

| Métrique | Détail demandé |
| --- | --- |
| Sommeil | Dernier score de sommeil (0 à 100) |
| Pas | Pas du jour + objectif quotidien (barre de progression) |
| Stress | Score de stress (0 à 100) |
| Cardio | Fréquence cardiaque, moyenne par minute compilée toutes les 2 minutes |

Ce document fige la conception avant le câblage transport + UI. Le cœur de
données (`src/health_data`) est déjà écrit et testé ; il ne reste que le
transport et l'affichage, qui dépendent tous deux de la décision ci-dessous.

## Le point de décision : comment les données arrivent à la montre

C'est le seul vrai inconnu de la phase, et il faut une réponse avant de câbler
quoi que ce soit côté BLE.

Aujourd'hui la montre se présente à Gadgetbridge comme une **InfiniTime**
(serveur GATT, service Alert Notification `0x1811`, voir `src/ans.cpp`).
Gadgetbridge **pousse** vers la montre : notifications, heure, météo. Il
synchronise séparément les données santé de l'Amazfit dans **sa propre base**,
mais **ne transfère jamais** les métriques d'un appareil vers un autre. Donc le
chemin standard Gadgetbridge n'amène pas le cardio/sommeil/pas de l'Amazfit vers
la montre. Il faut un **relais actif côté téléphone**.

### Options de transport

1. **Tasker + intents Gadgetbridge → caractéristique GATT custom sur la montre**
   (recommandé). Gadgetbridge diffuse des broadcasts Android
   (`nodomain.freeyourgadget.gadgetbridge.action...`) et expose des données
   d'activité. Une automatisation Tasker (ou une petite app compagnon) lit ces
   valeurs et les écrit dans une caractéristique GATT dédiée que la montre
   expose. Aucune app à publier, la montre reste maître du format.
   - Pour : pas de nouvelle app à maintenir, la montre définit le contrat.
   - Contre : il faut vérifier quels champs santé Gadgetbridge expose réellement
     en broadcast/DB pour l'Amazfit Helio (à confirmer par l'utilisateur).

2. **Petite app compagnon Android dédiée**. Lit la base/les intents de
   Gadgetbridge et pousse en BLE vers la montre. Plus de contrôle, mais une app
   à écrire et maintenir.

3. **Piggyback sur un canal existant** (ex. détourner un champ météo/CTS). Fragile
   et hors specs ; écarté.

**Recommandation** : option 1. La montre expose une caractéristique d'entrée
santé ; le relais côté téléphone (Tasker d'abord, app compagnon si besoin) y
écrit un petit paquet binaire. Le firmware est alors complet et testable
indépendamment du choix final côté téléphone.

## Caractéristique GATT d'entrée santé (implémentée)

Ajoutée au serveur GATT déjà actif dans `src/ans.cpp` : une caractéristique
**write** unique posée **sur le service Alert Notification déjà exposé**
(`0x1811`). Les octets reçus sont copiés dans une mailbox depuis la tâche BLE,
puis appliqués au modèle par la boucle (`health_ingest_packet` →
`health_tick_1hz`), donc le modèle reste mono-thread.

- Service : `00001811-0000-1000-8000-00805f9b34fb` (Alert Notification, déjà annoncé)
- Écriture santé : `a2470002-5a4b-4d55-9a3e-1c2d3e4f5a6b`

> **Pourquoi sur `0x1811` et non un 3ᵉ service dédié.** Un troisième service
> GATT vendeur (`a2470001-…`) ne s'enregistrait pas de façon fiable sur cette
> pile BLE (Bluedroid Arduino) : l'app compagnon ne trouvait alors « aucun
> service santé ». En repliant la caractéristique sur le service ANS — toujours
> annoncé et découvert avec les notifications — elle est systématiquement
> visible. Gadgetbridge ignore cette caractéristique additionnelle ; seule l'app
> compagnon y écrit. L'app cherche d'abord la caractéristique sous `0x1811`, avec
> repli sur l'ancien service `a2470001-…` pour un firmware plus ancien.

### CAVEAT à valider sur la montre (modèle de connexion)

La montre est déjà un périphérique BLE **connecté à Gadgetbridge** (vue comme une
InfiniTime). Pour que le relais (Tasker/app compagnon) atteigne la
caractéristique santé, il doit se connecter comme **second central** pendant que
Gadgetbridge tient déjà la connexion. Selon la config Bluedroid (nombre de
connexions ACL) et la reprise d'annonce après connexion, ce second central peut
être refusé. À vérifier sur matériel quand tu montes le relais : si ça bloque, on
ajoutera la reprise d'annonce sur `onConnect` (multi-central) ou on connectera le
relais quand Gadgetbridge est déconnecté. La caractéristique est posée de façon
**additive** : elle ne touche pas le chemin notifications éprouvé.

### Format du paquet

Paquet compact, `little-endian`, extensible :

```
octet 0      : version du format (= 1)
octet 1      : masque de champs présents (bit0 sommeil, bit1 pas,
               bit2 objectif, bit3 stress, bit4 cardio-moyenne)
octets 2..   : champs présents, dans l'ordre des bits :
   sommeil   : u8   (0..100)
   pas       : u32
   objectif  : u32
   stress    : u8   (0..100)
   cardio    : u16  (bpm déjà moyenné côté téléphone)  -> set_hr_avg()
```

Le relais n'envoie que ce qu'il a (masque), à la fréquence qu'il veut. Le
firmware met à jour `HealthData` via les setters correspondants. Si plus tard on
préfère envoyer des échantillons cardio bruts plutôt qu'une moyenne, un bit5
« cardio-échantillon » appellera `add_hr_sample()` et la montre compilera la
moyenne 2 min elle-même (déjà implémenté).

## Cœur de données (déjà fait, testé)

`src/health_data.h` / `.cpp` : modèle pur, sans Arduino/BLE/NVS, testé sur hôte
(`test/test_health_data.cpp`, dans la suite CI « Host suite »). Il gère :

- les quatre métriques + l'objectif de pas, avec bornage 0..100 des scores ;
- la **fenêtre cardio de 2 minutes** : `add_hr_sample()` accumule, `tick()`
  publie la moyenne arrondie au passage de la borne ; ou `set_hr_avg()` pour une
  valeur déjà moyennée ;
- la **péremption** par métrique (`*_stale()`) pour griser une valeur quand le
  relais s'est tu (cardio 6 min, stress 15 min, pas 30 min, sommeil 26 h) ;
- la progression d'objectif (`step_progress_pct()`, `step_goal_reached()`) ;
- un **snapshot** pour le cache NVS : au reboot on réaffiche les dernières
  valeurs connues (grisées tant que le relais n'a pas rafraîchi) au lieu de vide.

Le temps est injecté (ms) à chaque appel : logique déterministe, testable, et
correcte au débordement de `millis()`.

## Décisions actées (réponses utilisateur)

1. **Transport** : caractéristique GATT custom, alimentée par Tasker / app
   compagnon lisant Gadgetbridge. Fait.
2. **Champs Gadgetbridge** : confirmé (sommeil + stress + cardio disponibles).
3. **Affichage** : nouvel écran « Santé » dédié, ouvert par une tuile « Health »
   (icône cœur) dans la grille « Tools » (swipe haut). Fait.
   De plus, la ligne d'accent sous l'heure du cadran Dot devient une **barre de
   progression de pas** : rail **rouge** plein, remplissage **blanc** qui grandit
   avec les pas ; tout rouge à 0 %, tout blanc à l'objectif. Fait.
4. **Objectif de pas** : valeur fixe dans les Settings (défaut 10000). Fait. Le
   `BIT_GOAL` du paquet est donc ignoré (les Settings font foi).

## État de l'implémentation

- Cœur `health_data` (pur, testé) : fait.
- Runtime `health_state` (singleton, NVS `argushealth`, tick 1 Hz, mailbox
  BLE→boucle, objectif de pas) : fait.
- Barre de progression de pas sur le cadran Dot : fait.
- Réglage objectif de pas (slider Settings) : fait.
- Écran « Santé » (sommeil, pas + objectif, stress, cardio) : fait.
- Caractéristique GATT d'entrée + parsing du paquet : fait.

## Ce qu'il reste (validation matérielle + téléphone)

1. **Relais côté téléphone** : monter Tasker (ou l'app compagnon) qui lit
   Gadgetbridge et écrit le paquet dans la caractéristique santé.
2. **Modèle de connexion** : valider le second central (voir le CAVEAT plus
   haut). Ajuster si nécessaire.
3. **Format cardio** : confirmer si le relais envoie une moyenne (`BIT_HR_AVG`)
   ou des échantillons bruts (`BIT_HR_SAMPLE`, la montre compile alors la
   moyenne 2 min).
