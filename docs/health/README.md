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

## Contrat proposé : caractéristique GATT d'entrée santé

À ajouter au serveur GATT déjà actif dans `src/ans.cpp` (pas de second service à
annoncer, pas de contention radio supplémentaire). UUID vendeur 128 bits, une
caractéristique **write** unique. Paquet compact, `little-endian`, extensible :

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

## Ce qu'il reste (prochaine session)

1. **Transport** (après décision utilisateur) : ajouter la caractéristique GATT
   d'entrée à `src/ans.cpp`, parser le paquet, alimenter `HealthData`.
2. **Persistance** : fine couche device (`Preferences`, namespace `argushealth`)
   qui sérialise/désérialise le `Snapshot` au boot et périodiquement.
3. **Tick** : appeler `health_tick()` dans le bloc 1 Hz de `main.cpp`.
4. **UI** : où afficher ? À décider (voir questions). Le rendu réutilisera la
   palette Dot (blanc actif, gris repos, rouge accents).

## Questions ouvertes pour l'utilisateur

1. **Transport** : on part sur l'option 1 (Tasker/compagnon → caractéristique
   GATT custom) ? Sinon laquelle ?
2. **Champs Gadgetbridge** : peux-tu confirmer que Gadgetbridge expose bien, pour
   ton Helio, le score de sommeil, le stress et le cardio (et pas seulement les
   pas) via broadcast/DB accessible à Tasker ? Ça décide si l'option 1 suffit.
3. **Affichage** : où veux-tu voir ces métriques ?
   - un nouvel écran « Santé » (swipe dédié), ou
   - intégrées au cadran Dot (ex. sous la date), ou
   - une tuile dans Tools ?
4. **Objectif de pas** : valeur fixe (ex. 10000) réglée dans les Settings, ou
   poussée par le relais depuis Gadgetbridge ?
