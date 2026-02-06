Piloter 8 relais depuis un écran Nextion connecté à un Arduino (Mega), avec 3 modes :

MANUAL : sélection directe d’un relais (toggle ON/OFF).

TIMED : activation d’un relais pendant une durée paramétrable de 0 à 10 seondes, puis arrêt automatique.

Ce mode permet d'utiliser les relais mecaniques Transco jusqu'a 8 voies.

ICOM : sélection automatique du relais en fonction d’une tension analogique sur A0 (issue d’un poste ICOM via réseau résistif / sorties band-data), avec filtrage anti-bruit.

*********   ATTENTION / WARNNING    les ICOM delivrent une tension de 8V, vous devez utiliser un pont diviseur sur A0 pour transposer en 5V, voir le code  **********

<img width="480" height="318" alt="maintenu" src="https://github.com/user-attachments/assets/a799fcf8-d24c-43b3-b2ca-1596346e2be2" />


<img width="481" height="322" alt="temporisé" src="https://github.com/user-attachments/assets/2db2067c-5332-4546-abd7-1a55dc8ac104" />


<img width="475" height="318" alt="icom" src="https://github.com/user-attachments/assets/c90c05df-2ef6-4fc6-bbf4-9953edee97e5" />
