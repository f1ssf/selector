Piloter 8 relais depuis un écran Nextion connecté à un Arduino (Mega), avec 3 modes :

MANUAL : sélection directe d’un relais (toggle ON/OFF).

TIMED : activation d’un relais pendant une durée paramétrable de 0 à 10 seondes, puis arrêt automatique.

Ce mode permet d'utiliser les relais mecaniques Transco jusqu'a 8 voies.

ICOM : sélection automatique du relais en fonction d’une tension analogique sur A0 (issue d’un poste ICOM via réseau résistif / sorties band-data), avec filtrage anti-bruit.

*********   ATTENTION / WARNNING    les ICOM delivrent une tension de 8V, vous devez utiliser un pont diviseur sur A0 pour transposer en 5V, voir le code  **********

![IMG_1169](https://github.com/user-attachments/assets/9f51cea3-da0e-4479-b36c-304dff3df2b2)


![IMG_1170](https://github.com/user-attachments/assets/ae016195-6cd7-488f-8412-ec4d40d60ef9)


![IMG_1171](https://github.com/user-attachments/assets/40c61eb2-739c-4073-980a-ac5abb766dda)


![IMG_1169](https://github.com/user-attachments/assets/4610b987-5a42-4ab1-96ad-1c62694c1e45)
