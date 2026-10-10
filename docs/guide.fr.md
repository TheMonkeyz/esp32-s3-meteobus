<!-- source: README.md @ ad04dede698940cabd986f6cb48f748272f95057; sections: What you need, Install, First-time setup, Using it, Settings, Presence dimming, Languages, Updates over Wi-Fi, Data sources -->

<p align="center">
  <img src="img/hero.png" width="720" alt="Trois écrans ronds de l'afficheur : la page d'un arrêt d'autobus, l'écran météo et le radar de pluie">
</p>

<h1 align="center">Guide de MeteoBus</h1>

<p align="center">
  <b>Micrologiciel pour la Waveshare ESP32-S3-Touch-AMOLED-1.75</b><br>
  La météo locale, des prévisions heure par heure, un radar de pluie en direct et les prochains départs de vos arrêts
  d'autobus du RTC (Québec) sur un écran AMOLED rond. Aucune clé d'API requise.
</p>

<p align="center">
  <a href="https://themonkeyz.github.io/esp32-s3-meteobus/?lang=fr"><b>Installer dans le navigateur</b></a> ·
  <a href="https://github.com/TheMonkeyz/esp32-s3-meteobus/releases">Versions</a> ·
  <a href="../CHANGELOG.md">Journal des modifications</a>
</p>

<p align="center"><sub>Ce guide est la traduction des sections du <a href="../README.md">README en anglais</a>
  destinées aux propriétaires de l'afficheur. Le README en anglais demeure la référence.</sub></p>

<table align="center">
  <tr>
    <td align="center"><img src="../web/flash/img/weather-fr.png" width="180" alt="Écran météo : horloge, ville, température et conditions, ressenti, humidité et vent, et les prévisions sur 3 jours"><br><a href="#écran-météo"><b>Météo</b></a><br><sub>Maintenant, les 2 prochaines heures et 3 jours</sub></td>
    <td align="center"><img src="../web/flash/img/stop.png" width="180" alt="Page d'un arrêt d'autobus : le nom et le numéro de l'arrêt, le numéro du parcours et le prochain départ en minutes, la direction et les trois départs suivants"><br><a href="#autobus"><b>Autobus</b></a><br><sub>Les prochains départs de vos arrêts du RTC</sub></td>
    <td align="center"><img src="../web/flash/img/busmap.png" width="180" alt="Carte du parcours : les rues autour de l'arrêt, le tracé du parcours et ses autobus en route"><br><a href="#carte-du-parcours"><b>Carte du parcours</b></a><br><sub>Le parcours et ses autobus, en direct</sub></td>
    <td align="center"><img src="../web/flash/img/radar.png" width="180" alt="Écran radar : la pluie sur une carte assombrie, avec un cercle de distance et l'heure du radar"><br><a href="#radar"><b>Radar</b></a><br><sub>Pluie et foudre, boucle de 3 heures</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="../web/flash/img/hourly.png" width="180" alt="Vue horaire : le graphique des températures de la journée et une rangée par heure avec la température, la probabilité de pluie et le vent"><br><a href="#vue-horaire"><b>Vue horaire</b></a><br><sub>7 jours, heure par heure</sub></td>
    <td align="center"><img src="../web/flash/img/extras-fr.png" width="180" alt="Écran Extras : date, arc du soleil du lever au coucher, indice UV, phase de la lune et qualité de l'air"><br><a href="#extras"><b>Extras</b></a><br><sub>Soleil, UV, lune, qualité de l'air</sub></td>
    <td align="center"><img src="../web/flash/img/status.png" width="180" alt="Écran État : version du micrologiciel, canal de mise à jour, signal Wi-Fi et chaque service en ligne avec un point de couleur et son temps de réponse"><br><a href="#état"><b>État</b></a><br><sub>Version, Wi-Fi, services en ligne</sub></td>
    <td align="center"><img src="../web/flash/img/settings-fr.png" width="180" alt="Écran des réglages : Tamiser si calme, Réveil si soulevé, Délais, Température, et la bande de luminosité en bas"><br><a href="#sur-lafficheur"><b>Réglages</b></a><br><sub>Appui long sur un écran principal</sub></td>
  </tr>
</table>

MeteoBus réunit deux projets antérieurs pour la même carte : l'afficheur météo
([esp32-s3-weather](https://github.com/TheMonkeyz/esp32-s3-weather), dont il est parti, à la v1.15.0) et l'afficheur
d'autobus du RTC ([esp32-s3-rtcquebec](https://github.com/TheMonkeyz/esp32-s3-rtcquebec)).

## 🧰 Ce qu'il vous faut

- Une **Waveshare ESP32-S3-Touch-AMOLED-1.75** : ESP32-S3 avec 16 Mo de mémoire flash et 8 Mo de PSRAM, écran AMOLED
  rond de 1,75 pouce (dalle CO5300 de 466×466), contrôleur tactile CST9217. Le micrologiciel utilise aussi ses deux
  microphones (écran tamisé selon la présence), son haut-parleur (sons d'alerte) et son détecteur de mouvement
  (réveil quand on le soulève).
- Un **câble de données USB-C** et un ordinateur avec **Chrome ou Edge** pour la première installation.
- Un **réseau Wi-Fi** avec accès à Internet, et un **téléphone** pour la configuration Wi-Fi et la page de réglages.
  L'ESP32-S3 ne prend en charge que le Wi-Fi à 2,4 GHz.
- Aucun compte ni clé d'API. D'où viennent les données et quelles régions elles couvrent : voir
  [Sources des données](#-sources-des-données).

## 🔌 Installation

Ouvrez l'**[outil d'installation Web](https://themonkeyz.github.io/esp32-s3-meteobus/?lang=fr)** dans Chrome ou Edge
sur un ordinateur, branchez l'afficheur avec un câble de données USB-C et suivez les étapes. L'outil offre deux
canaux : **Stable** (la dernière version) et **Bêta** (une version candidate, offerte seulement quand elle est plus
récente que la dernière version).

> 💡 **Astuce :** cochez **Erase device** (effacer l'appareil) la première fois. Laissez la case décochée pour les mises à jour, afin
> de conserver le Wi-Fi et les réglages. Un afficheur effacé démarre comme un neuf : configuration Wi-Fi, l'endroit
> intégré (la ville de Québec) jusqu'à ce que vous choisissiez le vôtre, aucun arrêt, un nouveau certificat pour la
> page de réglages (le téléphone vous avertit de nouveau une fois) et une nouvelle clé de réglages; les cartes du
> radar se téléchargent de nouveau.

Autres façons :

- Des images prêtes à installer sont aussi jointes à chaque
  [version](https://github.com/TheMonkeyz/esp32-s3-meteobus/releases). Les notes de version donnent les commandes
  esptool : l'image complète pour une première installation (efface les réglages enregistrés), ou les parties
  séparées pour une mise à jour qui les conserve.
- Sous Windows, à partir de votre propre compilation : voir
  [Flashing on Windows](../README.md#flashing-on-windows) (en anglais).
- Une fois le micrologiciel installé, l'afficheur se met à jour lui-même par Wi-Fi : voir
  [Mises à jour par Wi-Fi](#-mises-à-jour-par-wi-fi).

Si l'ordinateur ne trouve pas l'afficheur, maintenez **BOOT** enfoncé, appuyez brièvement sur **RESET**, relâchez
**BOOT**, puis réessayez.

## 📶 Première configuration

1. Installez le micrologiciel (voir [Installation](#-installation)). Quand aucun réseau Wi-Fi n'est enregistré,
   l'écran affiche **Configuration Wi-Fi** et un code QR.
2. Balayez le code QR pour vous connecter au réseau de l'afficheur, **MeteoBus-Setup**. Son mot de passe est affiché
   sous le code : chaque afficheur a le sien.
3. Le téléphone ouvre de lui-même son écran de connexion au réseau (portail captif), qui présente la page de
   configuration, avec la section Wi-Fi en haut. Sinon, ouvrez **http://192.168.4.1**.
4. La page recherche les réseaux automatiquement et affiche ceux qui sont à proximité (le plus fort en premier,
   🔒 = mot de passe requis). Touchez le vôtre, entrez le mot de passe (**Afficher** le montre pendant la saisie) et
   touchez **Enregistrer le Wi-Fi et redémarrer**. L'afficheur redémarre et se connecte. **📶 Rechercher de nouveau**
   actualise la liste.
5. Choisissez ensuite votre endroit et ajoutez vos arrêts d'autobus sur la page de réglages (voir
   [Réglages](#-réglages)). D'ici là, l'afficheur montre la météo de la ville de Québec, et l'écran des autobus
   explique comment ajouter des arrêts.

### Wi-Fi sans saisir le mot de passe

L'écran de configuration Wi-Fi a deux pages; **glissez** pour passer de l'une à l'autre :

1. **Tous les téléphones :** un code QR pour se connecter à **MeteoBus-Setup**; la page de configuration s'ouvre
   d'elle-même (comme ci-dessus).
2. **Android 10+ — Easy Connect :** sur la page « Android : Easy Connect », pendant que le téléphone est connecté au
   Wi-Fi voulu, balayez le code de l'afficheur (avec la caméra ou n'importe quelle appli QR). Le téléphone envoie ce
   réseau, mot de passe compris; l'afficheur l'enregistre et redémarre.

Les navigateurs ne peuvent pas lire les mots de passe Wi-Fi enregistrés sur un téléphone, et l'iPhone ne prend pas
en charge Easy Connect. Sur iPhone, l'astuce *Mot de passe enregistré sur votre téléphone? Copiez-le* de la page de
configuration explique comment le copier : Réglages → Wi-Fi → ⓘ → Mot de passe → Copier.

## 📱 Utilisation

Les écrans principaux sont placés côte à côte : **État · Extras · Météo · Autobus**. À partir de l'écran météo,
glissez vers la **droite** pour la page Extras (et encore vers la droite pour la page État), vers la **gauche** pour
vos arrêts d'autobus. Trois écrans s'ouvrent par-dessus et se ferment d'un **glissement de côté** : le radar (touchez
l'icône météo), la carte du parcours (touchez le numéro du parcours d'un arrêt) et les détails d'une alerte (touchez
la pastille du bas). **Appuyez longuement** sur l'écran météo ou sur la page d'un arrêt pour les réglages.

### Écran météo

<img align="right" width="200" src="../web/flash/img/weather-fr.png" alt="Écran météo">

Horloge, nom de l'endroit, icône et température, conditions, puis ressenti, humidité (goutte bleue) et vent (symbole
du vent). Une ligne indique quand la pluie ou la neige commence ou cesse d'ici 2 h (« Pluie vers 14:45 »). Puis les
températures maximales et minimales sur 3 jours, avec des icônes. En bas, une pastille montre une alerte météo dans sa
couleur ou, sans alerte, une mise à jour qui attend (voir [Alertes météo](#alertes-météo)).

Avec plusieurs endroits, il y a une page par endroit (points sur le bord droit), chacune avec son heure locale.

- **Glissez vers le haut ou le bas** pour changer d'endroit. La page suit le doigt, se met en place et rebondit au
  premier et au dernier endroit.
- **Touchez l'icône météo** (elle porte un petit insigne de radar) pour le radar.
- **Touchez un jour** des prévisions pour sa vue horaire.
- **Touchez la pastille** du bas pour les détails de l'alerte ou la mise à jour.
- **Glissez vers la droite** pour la page Extras, **vers la gauche** pour vos arrêts d'autobus.
- **Appuyez longuement** pour ouvrir l'écran des réglages.

<br clear="right">

### Autobus

<img align="right" width="200" src="../web/flash/img/stop.png" alt="Page d'un arrêt d'autobus">

Une page par arrêt favori, jusqu'à 8 (ajoutez-les sur la page de réglages du téléphone, section **Mes arrêts**),
disposée comme l'écran météo : l'heure de la mise à jour, l'horloge, le nom et le numéro de l'arrêt, le numéro du
parcours et le prochain départ en gros, la direction, l'origine de l'heure (**Temps réel**, selon le GPS de
l'autobus, ou **Horaire prévu**) et les trois départs suivants. La page indique aussi un départ **Annulé**, **Plus de
départs aujourd'hui**, un **Arrêt non desservi pour le moment** (détour, travaux) et un arrêt en **Descente
seulement**.

L'arrêt affiché est demandé au RTC toutes les 30 s pendant que l'écran des autobus est affiché; les autres, toutes
les 5 minutes. Les avis du parcours (un détour, un arrêt déplacé) s'affichent dans une pastille orange en bas :
touchez-la pour le texte complet.

- **Glissez vers le haut ou le bas** pour changer d'arrêt (points sur le bord droit).
- **Touchez le numéro du parcours** (il porte une petite épingle) pour la [carte du parcours](#carte-du-parcours).
- **Touchez la pastille orange** pour les avis du parcours; glissez de côté pour revenir.
- **Glissez vers la droite** pour la météo. **Appuyez longuement** pour les réglages.

Sans arrêt, l'écran explique comment en ajouter : appuyez longuement, puis **Endroit et plus (tél.)**.

<br clear="right">

### Carte du parcours

<img align="right" width="200" src="../web/flash/img/busmap.png" alt="Carte du parcours">

Les rues autour de l'arrêt (OpenStreetMap), le tracé du parcours dans sa direction, l'arrêt et les autobus du
parcours en route, sous forme de petites icônes d'autobus actualisées toutes les 20 s.

- **Glissez vers le bas** pour un zoom avant, **vers le haut** pour un zoom arrière (5 paliers, du quartier à
  quelques rues). La carte grandit ou rapetisse aussitôt sous le doigt; la carte plus nette la remplace un moment
  plus tard, et les autobus reviennent une fois qu'elle est immobile.
- **Glissez de côté** pour revenir à l'arrêt (elle revient aussi d'elle-même après 5 minutes sans toucher). Toucher
  la carte ne fait rien.

<br clear="right">

### Vue horaire

<img align="right" width="200" src="../web/flash/img/hourly.png" alt="Vue horaire">

Les prévisions sur 7 jours (l'écran météo montre les 3 premiers). Pour chaque jour : le jour de la semaine, les
conditions et les températures maximale et minimale; le graphique des températures de la journée (de 0 à 24 h, une
ligne par heure, maximum et minimum indiqués; aujourd'hui, les heures passées sont en gris, avec un point à l'heure
actuelle); puis une rangée par heure : heure, icône, température, probabilité de pluie, vent. Aujourd'hui commence à
l'heure en cours (« Actuel »).

- **Glissez vers le haut ou le bas** pour faire défiler les heures.
- **Glissez vers la gauche ou la droite** pour changer de jour. La page suit le doigt et se met en place.
- **Touchez** pour fermer.

<br clear="right">

### Radar

<img align="right" width="200" src="../web/flash/img/radar.png" alt="Écran radar">

La région autour de l'endroit affiché : une carte OpenStreetMap assombrie, le radar d'Environnement Canada, la foudre
des 10 dernières minutes (éclairs jaunes), un cercle de distance, l'horloge, ainsi que l'heure et le rayon du radar.
Les 3 dernières heures se chargent en quelques secondes après l'ouverture.

- **Touchez** pour lire les 3 dernières heures (15 images, 3 images par seconde), en boucle pendant une minute (les
  nouvelles images radar s'ajoutent à la boucle). Touchez de nouveau pour arrêter.
- **Glissez vers le bas** pour un zoom avant, **vers le haut** pour un zoom arrière : d'un rayon d'environ 25 km
  à environ 1 550 km, en 7 paliers (le rayon double à chaque palier), avec animation.
- **Glissez de côté** pour revenir (le radar se ferme aussi de lui-même après 5 minutes sans toucher).

<br clear="right">

### Extras

<img align="right" width="200" src="../web/flash/img/extras-fr.png" alt="Écran Extras">

- La date.
- L'arc du soleil, du lever au coucher, avec le soleil à l'heure actuelle (durée du jour, ou le prochain lever du
  soleil la nuit).
- L'indice UV actuel et le maximum du jour.
- La phase de la lune, avec image et pourcentage éclairé.
- La qualité de l'air (indice américain US AQI).
- Le pollen quand il est offert (Open-Meteo ne le fournit que pour l'Europe, alors la rangée est masquée au Canada).

**Glissez vers la gauche** pour revenir, **vers la droite** pour la page État.

<br clear="right">

### État

<img align="right" width="200" src="../web/flash/img/status.png" alt="Écran État">

Glissez deux fois vers la droite à partir de l'écran météo.

- La version du micrologiciel, le canal de mise à jour et l'emplacement de l'application.
- Le signal Wi-Fi, l'adresse IP et la durée de fonctionnement.
- Chaque service en ligne utilisé par l'afficheur (prévisions et qualité de l'air d'Open-Meteo, alertes et radar
  d'Environnement Canada, OpenStreetMap, le RTC, GitHub Pages pour les mises à jour, le serveur de temps), avec un
  point de couleur, le moment du dernier contact, le temps de réponse ou la raison de l'échec. L'ouverture de la page
  vérifie tout service qui n'a pas été contacté depuis 5 min.

**Glissez vers la gauche** pour revenir; glissez vers le haut ou le bas pour faire défiler.

<br clear="right">

### Alertes météo

Les veilles, avertissements, avis et bulletins d'Environnement Canada pour l'endroit affiché apparaissent dans une
pastille au bas de l'écran météo, de la couleur de l'alerte (`+1` s'il y en a d'autres); le nom de l'endroit reste en
haut. Touchez la pastille pour les détails : une carte de la région touchée sur OpenStreetMap, jusqu'à quand l'alerte
est en vigueur (« Jusqu'à … »), la région et le texte. Glissez vers le haut ou le bas pour faire défiler; glissez de
côté pour revenir.

Quand une mise à jour attend aussi, l'alerte garde la pastille, qui porte alors un petit point bleu; les détails de
l'alerte se terminent par **Nouvelle version >**, qui ouvre la mise à jour.

La pastille orange des avis d'un arrêt ouvre le même écran, avec les avis du RTC pour ce parcours.

### Sons d'alerte

Des bips d'avertissement retentissent dans le haut-parleur quand une nouvelle alerte météo apparaît pour l'endroit
affiché : jaune, 2 bips; orange, 3 + 3 bips; rouge, une sirène à deux tons (aigu-grave). Chaque alerte ne sonne
qu'une fois. Choisissez les alertes qui sonnent, le volume et les heures de silence (les alertes rouges sonnent quand
même) dans les [Réglages](#-réglages).

## 🔧 Réglages

Chaque réglage est enregistré aussitôt et conservé lors des mises à jour. L'écran des réglages de l'afficheur offre
ceux de tous les jours; la page de réglages du téléphone les offre tous.

### Sur l'afficheur

<img align="right" width="200" src="../web/flash/img/settings-fr.png" alt="Écran des réglages de l'afficheur">

**Appuyez longuement** sur l'écran météo ou sur la page d'un arrêt.

- **ÉCRAN :** Tamiser si calme, Réveil si soulevé, Délais (Courts / Normaux / Longs), luminosité.
- **UNITÉS :** Température, Vent, Horloge, Langue (English / Français / ᐃᓄᒃᑎᑐᑦ).
- **SON :** Alerte sonore (Non / Rouge / Orange+ / Toutes), Volume, Essayer le son.
- **PLUS :** **Endroit et plus (tél.)** (le code QR de la page du téléphone), Réseau Wi-Fi, Mises à jour,
  Redémarrer.

Touchez une rangée pour l'activer ou la changer. Glissez le long de la bande du bas pour régler la luminosité (elle
suit le doigt). Touchez **OK** ou glissez vers la **droite** pour fermer et revenir à l'écran d'où vous l'avez
ouvert. Il faut toucher **Redémarrer** deux fois (« Touchez encore »).

<br clear="right">

### Sur votre téléphone

Appuyez longuement sur l'afficheur, touchez **Endroit et plus (tél.)** et balayez le code QR. Votre téléphone doit
être connecté au même réseau Wi-Fi. Le téléphone vous avertira que le certificat n'est pas fiable. C'est normal, car
l'afficheur signe son propre certificat; choisissez *Paramètres avancés → Continuer*. La page est servie en HTTPS,
ce qui permet au bouton **Utiliser la position du téléphone** de fonctionner.

Le code transmet aussi la **clé** de l'afficheur : une page ouverte à partir du code peut modifier les réglages, et
ce téléphone s'en souvient. Une page ouverte en tapant l'adresse affiche les réglages, mais demande de balayer le
code avant toute modification (voir les [notes de sécurité](../README.md#-security-notes), en anglais).

- **Endroits :** jusqu'à 4 (maison, chalet, travail…). Touchez un endroit pour le modifier (nom, recherche de ville,
  touchez la **carte** ou glissez l'épingle à l'endroit exact, ou *Utiliser la position du téléphone*), *Afficher*
  pour le voir sur l'afficheur, ou *＋ Ajouter un endroit*. La carte a besoin d'Internet sur le téléphone. Tout ce qui
  touche la météo suit l'endroit affiché : les alertes, la qualité de l'air, la vue horaire, la page Extras et le
  radar. Les prévisions de chaque endroit sont actualisées toutes les 10 minutes, donc elles apparaissent aussitôt;
  la carte du radar est gardée en mémoire pour le premier endroit seulement, alors les cartes des autres endroits se
  chargent en quelques secondes.
- **Mes arrêts :** jusqu'à 8 arrêts d'autobus. Entrez le **numéro d'arrêt** (inscrit sur le panneau de l'arrêt) et
  le **parcours**, touchez *Trouver les directions*, choisissez la direction, puis *Ajouter cet arrêt*; l'afficheur
  vérifie d'abord auprès du RTC, alors un parcours qui ne s'arrête pas là dans cette direction est refusé. *Monter*
  change l'ordre des pages; *Retirer* en supprime une. Les autobus ne suivent pas les endroits : ce sont ceux de
  Québec, à l'heure de Québec.
- **Unités :** langue, °C/°F, vent en km/h, mph ou m/s (les distances du radar suivent : milles avec mph, km
  autrement), horloge de 24 ou 12 heures. L'afficheur se redessine aussitôt.
- **Écran et présence :** indicateur de niveau sonore en direct, calibration, délais, luminosité, réveil quand on
  soulève l'afficheur (voir [Écran tamisé selon la présence](#-écran-tamisé-selon-la-présence)).
- **Son :** les alertes qui font sonner l'afficheur, le volume, les heures de silence, un essai.
- **Réseau Wi-Fi :** recherche des réseaux, choix, mot de passe.
- **Micrologiciel :** version, canal Stable ou Bêta, vérification, nouveautés, installation (voir
  [Mises à jour](#-mises-à-jour-par-wi-fi)).

### Wi-Fi : le changer, et quand l'afficheur ne peut pas se connecter

- **Changer de réseau :** appuyez longuement, puis touchez **Réseau Wi-Fi**. L'afficheur active le réseau
  **MeteoBus-Setup** en plus de sa connexion actuelle et affiche un code QR pour s'y connecter; la page de connexion
  s'ouvre ensuite sur le téléphone, comme lors de la première configuration. Touchez l'afficheur pour annuler
  (« Touchez pour annuler »); le réseau de configuration se désactive aussi après 10 min. Quand l'afficheur est hors
  ligne, le premier appui long mène directement au code QR de configuration Wi-Fi.
- **Quand le réseau enregistré est inaccessible** (déménagement, nouveau routeur, routeur qui redémarre encore
  après une panne de courant) :
  - Pendant que l'écran affiche *Connexion à …* ou *Prévisions en cours...*, un **appui long** active le réseau de
    configuration et affiche son code QR.
  - Après environ 30 s sans connexion, l'afficheur montre de lui-même le code QR de configuration (*… injoignable /
    Touchez pour réessayer*), pendant 15 minutes. Ensuite (une longue panne), il cesse d'ouvrir de lui-même le réseau
    de configuration et continue simplement d'essayer le réseau enregistré (*Nouvel essai en cours*); un appui long
    ouvre toujours la configuration.
  - Pendant que l'écran de configuration est ouvert, l'afficheur n'essaie pas de se connecter au réseau enregistré :
    cela gênerait le téléphone. Touchez l'écran pour réessayer le réseau enregistré (30 s), ou attendez : après
    5 minutes sans téléphone sur le réseau de configuration, il réessaie de lui-même, puis affiche de nouveau l'écran de
    configuration. Si vous enregistrez plutôt un nouveau réseau, il redémarre et se connecte à celui-ci.
- **Réinitialiser le Wi-Fi avec les boutons** (rarement nécessaire) : appuyez sur **RESET**, puis maintenez **BOOT**
  enfoncé environ 2 s pendant que l'écran affiche *Démarrage...*. Ne maintenez pas BOOT enfoncé *pendant* que vous
  appuyez sur RESET : cela met la puce en mode de programmation.

## 🌙 Écran tamisé selon la présence

Les microphones servent de détecteur de présence : pièce calme → écran tamisé → écran éteint. Un son soutenu (pas un
simple bruit sec), un toucher ou le fait de soulever l'afficheur (détecteur de mouvement) → l'écran se rallume. Tout
se règle dans la section **Écran et présence** de la page du téléphone (l'écran des réglages de l'afficheur offre les
interrupteurs et les délais).

Les deux microphones intégrés mesurent le niveau sonore de la pièce toutes les 0,1 s.

- **Calme** pendant *Tamiser après* → l'écran est tamisé. Calme pendant *Éteindre après* (durée totale de calme) →
  l'écran s'éteint.
- Pour que l'écran tamisé ou éteint **se rallume**, il faut *Rallumer après* secondes de son **soutenu**. Le son
  remplit une jauge de réveil et le silence la vide deux fois moins vite; ainsi, une conversation avec des pauses
  le rallume, mais pas une porte qui claque. Toucher l'écran le rallume toujours; le toucher qui rallume un écran
  noir est ignoré, pour qu'il ne compte pas aussi comme un glissement ou un toucher.
- **Calibrez** sur la page de réglages pendant que la pièce est calme : 5 s de mesure fixent le niveau du bruit de
  fond (90e centile). « Fort » signifie bruit de fond + *Sensibilité* dB.
- La section **Écran et présence** de la page affiche un indicateur en direct (repère orange = seuil de déclenchement),
  l'état (Actif / Tamisé / Écran éteint), la progression du réveil et la durée de calme, ce qui est pratique pour
  ajuster les réglages.
- **Délais prédéfinis** (les durées peuvent être saisies en s / min / h; toute modification d'une valeur
  fait passer à *Personnalisés*) :

  | Délais | Tamiser après | Éteindre après (calme total) | Rallumer après |
  |---|---|---|---|
  | Essai | 10 s | 30 s | 2 s |
  | Courts | 2 min | 15 min | 2 s |
  | **Normaux** (par défaut) | 10 min | 60 min | 3 s |
  | Longs | 30 min | 3 h | 3 s |

  Autres valeurs par défaut : sensibilité 10 dB, luminosité 100 % / tamisée 15 %. Les valeurs par défaut ne
  s'appliquent que si rien n'est enregistré dans la mémoire non volatile (NVS).
- **Réveil quand on soulève l'afficheur :** oui ou non, Élevée / Normale / Faible, avec un indicateur de mouvement en
  direct sur la page.

## 🌍 Langues

L'afficheur et la page de réglages sont offerts en **anglais**, en **français** (français canadien) et en
**inuktitut** (syllabaire). La page de l'outil d'installation Web est aussi offerte en anglais, en français et en
inuktitut.

<table align="center">
  <tr>
    <th>English</th>
    <th>Français</th>
    <th>ᐃᓄᒃᑎᑐᑦ</th>
  </tr>
  <tr>
    <td><img src="../web/flash/img/weather-en.png" width="200" alt="Écran météo en anglais : Clear sky, Feels, Today, Sat, Sun"></td>
    <td><img src="../web/flash/img/weather-fr.png" width="200" alt="Écran météo en français : Ciel dégagé, Ressenti, Aujourd'hui, Sam., Dim."></td>
    <td><img src="../web/flash/img/weather-iu.png" width="200" alt="Écran météo en inuktitut (syllabaire)"></td>
  </tr>
  <tr>
    <td><img src="../web/flash/img/extras-en.png" width="200" alt="Écran Extras en anglais : lever du soleil, indice UV, lune, qualité de l'air"></td>
    <td><img src="../web/flash/img/extras-fr.png" width="200" alt="Écran Extras en français : Vendredi 2 octobre, Lever du soleil, Indice UV, Lune, Qualité de l'air"></td>
    <td><img src="../web/flash/img/extras-iu.png" width="200" alt="Écran Extras en inuktitut (syllabaire)"></td>
  </tr>
  <tr>
    <td><img src="../web/flash/img/settings-en.png" width="200" alt="Écran des réglages en anglais"></td>
    <td><img src="../web/flash/img/settings-fr.png" width="200" alt="Écran des réglages en français"></td>
    <td><img src="../web/flash/img/settings-iu.png" width="200" alt="Écran des réglages en inuktitut"></td>
  </tr>
</table>

- Un seul choix pour l'afficheur et la page de réglages : la rangée **Langue** (**Language** en anglais) de l'écran
  des réglages de l'afficheur, ou le sélecteur de la section **Unités** de la page.
- Les alertes météo s'affichent dans la langue choisie (Environnement Canada les publie en anglais et en français;
  en inuktitut, elles s'affichent en anglais). Les notes « Nouveautés » restent en anglais.

> ℹ️ **Remarque :** le texte en inuktitut est une ébauche qu'aucune personne parlant couramment la langue n'a encore révisée, alors
> certains mots peuvent être erronés. Voir [docs/translations/](translations/).

## 🔄 Mises à jour par Wi-Fi

L'afficheur se met à jour lui-même à partir du
[site de l'outil d'installation Web](https://themonkeyz.github.io/esp32-s3-meteobus/?lang=fr) :

- Il vérifie s'il y a une mise à jour une minute après le démarrage, puis toutes les 6 heures. Quand une version plus
  récente existe, une pastille bleue **Mise à jour vX.Y.Z** apparaît au bas de l'écran météo (si une alerte météo est
  affichée, l'alerte garde la pastille, qui porte alors un point bleu). Touchez-la pour voir les
  **Nouveautés** depuis votre version, puis touchez **Installer**. L'afficheur télécharge la mise à jour, redémarre et
  conserve tous les réglages. Rien ne s'installe sans votre demande.
- La page de réglages a une section **Micrologiciel** : la version installée, **Mises à jour : Versions stables / Bêta
  (versions candidates)**, *Vérifier les mises à jour*, les nouveautés et *Installer*, avec une barre de progression.
- **Bêta** suit le canal Bêta de l'outil d'installation (étiquettes `vX.Y.Z-rc.N`) et revient à Stable quand il n'y
  a pas de version candidate plus récente.
- Sécurité : l'intégrité du téléchargement est vérifiée (en-tête de l'image, SHA-256), et il doit s'agir du
  micrologiciel de ce projet, à la version offerte, avant que la nouvelle version soit choisie. Il provient du site
  GitHub Pages du projet, en HTTPS; il n'est pas signé (voir les [notes de sécurité](../README.md#-security-notes),
  en anglais). Une nouvelle version est conservée une fois qu'elle a fonctionné une minute en étant connectée au
  Wi-Fi (dix minutes sans Wi-Fi); si l'afficheur redémarre avant (plantage, boucle de redémarrage, panne de courant),
  il revient de lui-même à la version précédente et l'indique sur son écran de mise à jour. Un redémarrage demandé
  pendant cette minute attend qu'elle soit écoulée.
- **Si vous avez l'afficheur météo** ([esp32-s3-weather](https://github.com/TheMonkeyz/esp32-s3-weather)) : il se
  met à jour à partir de son propre site, alors il n'offre jamais MeteoBus. Installez MeteoBus une fois par USB avec
  l'outil d'installation Web. Avec *Erase device* décoché, il lit le Wi-Fi et les réglages enregistrés par l'afficheur
  météo (il est parti de la v1.15.0 de ce micrologiciel, avec la même organisation de la mémoire flash); si quelque
  chose cloche, réinstallez-le avec la case cochée.

Ce qui a changé à chaque version : [CHANGELOG.md](../CHANGELOG.md) (en anglais).

## 🌐 Sources des données

- Alertes : Environnement Canada, [API OGC de MSC GeoMet](https://api.weather.gc.ca), collection `weather-alerts`.
- Météo : [Open-Meteo](https://open-meteo.com) (conditions actuelles, prévisions quotidiennes et horaires sur
  7 jours, `timezone=auto`).
- Radar : [ECCC MSC GeoMet](https://eccc-msc.github.io/open-data/msc-data/obs_radar/readme_radar_geomet_en/) WMS,
  couche `RADAR_1KM_RRAI` (le Canada et la région frontalière du nord des États-Unis, 1 km, toutes les 6 min,
  3 dernières heures).
- Foudre : même service, couche `Lightning_2.5km_Density` (Réseau canadien de détection de la foudre, 2,5 km,
  toutes les 10 min, 3 dernières heures, le Canada et jusqu'à 250 km au-delà). Chaque image radar montre les éclairs
  de sa fenêtre de 10 minutes.
- Fond de carte : tuiles standard d'OpenStreetMap (zoom 4 à 10, un niveau par palier de zoom du radar). Après le
  démarrage, chaque niveau qui n'est pas encore en mémoire se télécharge en arrière-plan (environ 45 s pour les 7) et
  est enregistré dans la mémoire flash, alors le zoom est instantané par la suite. Si vous ouvrez le radar avant la
  fin, un panneau « Préparation des cartes » montre la progression. L'écran radar mentionne OpenStreetMap et ECCC;
  la carte des détails d'une alerte mentionne OpenStreetMap.

- Autobus : le [RTC](https://www.rtcquebec.ca) (Réseau de transport de la Capitale, Québec), par l'API de son site
  Web (`api-iv.rtcquebec.ca` : départs, parcours, positions des autobus et tracés) et ses avis sur rtcquebec.ca. Le
  RTC n'a pas d'API publique, et celle-ci n'est pas documentée et peut changer : c'est pour un usage personnel.
  L'afficheur demande un arrêt à la fois, au rythme décrit sous [Autobus](#autobus). Les rues de la carte du
  parcours sont des tuiles OpenStreetMap (zoom 13 à 17), téléchargées pendant que la carte est ouverte et non
  enregistrées.

En bref : les prévisions fonctionnent partout dans le monde; le radar couvre le Canada et la région frontalière du
nord des États-Unis; les alertes météo et la foudre couvrent le Canada (la foudre jusqu'à environ 250 km au-delà);
les autobus sont ceux du RTC, à Québec seulement.

## 💻 Développement, sécurité et licence

L'installation sous Windows avec les scripts, les compilations automatiques (GitHub Actions), la compilation à
partir du code source, l'organisation du projet, les notes de sécurité et la licence sont décrites en anglais dans le
[README](../README.md) : voir [For developers](../README.md#-for-developers),
[Security notes](../README.md#-security-notes) et [License](../README.md#-license).
