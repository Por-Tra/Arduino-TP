extends Tile

# ---- CONSTANTES ----
const DEVICE_NAME = "sensor_i"
const SUB_HB = "hb_" + DEVICE_NAME
const SUB_GET_STATE = "get_" + DEVICE_NAME + "_state"
## Délai sans heartbeat au-delà duquel le capteur est considéré comme bloqué (3,5 périodes de 1 s).
const HB_TIMEOUT_S := 3.5
## Durée d'allumage de l'icône de heartbeat à chaque réception.
const HB_BLINK_S := 0.5

const SUB_STATUS_COUNTER = "status_" + DEVICE_NAME + "_counter"
const SUB_CMD_RESET = "cmd_" + DEVICE_NAME + "_reset"
const SUB_STATUS_BUTTON = "status_" + DEVICE_NAME + "_button"
const SUB_STATUS_SENSOR = "status_" + DEVICE_NAME + "_sensor"

const SUB_SET_LED_COLOR = "set_" + DEVICE_NAME + "_led_color"
const SUB_STATUS_LED_COLOR = "status_" + DEVICE_NAME + "_led_color"

const SUB_CMD_LED_STATE = "cmd_" + DEVICE_NAME + "_led_state"
const SUB_STATUS_LED_STATE = "status_" + DEVICE_NAME + "_led_state"
const UNKNOWN_NODE_ID := -1
const COUNTER_PAYLOAD_SIZE := 4
const BUTTON_PAYLOAD_SIZE := 1
const SENSOR_PAYLOAD_SIZE := 8
const LED_COLOR_PAYLOAD_SIZE := 3
const LED_STATE_PAYLOAD_SIZE := 1
const COUNTER_OFFSET := 0
const BUTTON_OFFSET := 0
const TEMPERATURE_OFFSET := 0
const HUMIDITY_OFFSET := 4
const LED_RED_OFFSET := 0
const LED_GREEN_OFFSET := 1
const LED_BLUE_OFFSET := 2
const LED_STATE_OFFSET := 0
const BUTTON_PRESSED_COLOR := Color(0x00d67bff)
const BUTTON_RELEASED_COLOR := Color(0x0090d5ff)
const LED_ON_COLOR := Color(0xff2147ff)
const LED_OFF_COLOR := Color(0x00d67bff)
const STALE_MODULATE := Color(1, 1, 1, 0.4)

# ---- VARIABLES ----
var _node_id: int = UNKNOWN_NODE_ID
var _seconds_since_hb := INF
# Variable "_connected" avec setter personnalisé pour appeler la MAJ de l'UI au changement d'état
var _connected: bool = false:
	set(value):
		if value == _connected: return
		_connected = value
		if _connected:
			_on_sensor_connected()
		else:
			_on_sensor_disconnected()

var _state_button_style: StyleBoxFlat

var _state_led := false
var _color_led:Color
var _led_button_style: StyleBoxFlat


# ---- Abonnements et réception des messages ----
# Indique les messages que la tuile souhaite recevoir.
func get_subscriptions() -> PackedStringArray:
	var subscriptions: PackedStringArray = []
	subscriptions.append(SUB_HB)
	subscriptions.append(SUB_STATUS_COUNTER)
	subscriptions.append(SUB_STATUS_BUTTON)
	subscriptions.append(SUB_STATUS_SENSOR)
	subscriptions.append(SUB_STATUS_LED_COLOR)
	subscriptions.append(SUB_STATUS_LED_STATE)

	return subscriptions


## Source principale du node_id : la passerelle annonce le capteur par son nom dès son enregistrement,
## avant ses premières publications (dont l'état republié dans onSessionOpened côté Arduino).
func on_node_online(node_id: int, _mac_address: String, device_name: String) -> void:
	if device_name != DEVICE_NAME: return
	_node_id = node_id  # nouvel identifiant possible après un redémarrage du capteur
	%IDLabel.text = "node_id: %d" % _node_id
	


# Signale la déconnexion du capteur dans la tuile.
func on_node_offline(node_id: int, _reason_code: int) -> void:
	if node_id == _node_id: _connected = false


# Désactive la tuile lorsque la liaison avec la passerelle est perdue.
func on_gateway_reachability_changed(is_gateway_reachable: bool) -> void:
	if is_gateway_reachable: return
	_connected = false
	_node_id = UNKNOWN_NODE_ID  # la passerelle peut redistribuer les identifiants en redémarrant


# Vérifie les messages reçus et affiche les états confirmés par le capteur.
func on_message(subject_name: String, source_id: int, payload: PackedByteArray) -> void:
	if subject_name == SUB_HB:
		if _node_id != UNKNOWN_NODE_ID and source_id != _node_id: return
		if payload.size() != MPSUtils.HEARTBEAT_PAYLOAD_SIZE: return
		_on_hb(source_id)
	if source_id != _node_id: return # ne traitez que les messages dédiés à votre capteur
	if subject_name == SUB_STATUS_COUNTER:
		if payload.size() != COUNTER_PAYLOAD_SIZE: return # le contrat : 4 octets, sinon on ignore
		%CounterLabel.text = str(payload.decode_u32(COUNTER_OFFSET))
		
		
	if subject_name == SUB_STATUS_BUTTON:
		if payload.size() != BUTTON_PAYLOAD_SIZE: return
		_set_button_state(bool(payload.decode_u8(BUTTON_OFFSET)))



	if subject_name == SUB_STATUS_SENSOR:
		if payload.size() != SENSOR_PAYLOAD_SIZE:
			return
		var temperature := payload.decode_float(TEMPERATURE_OFFSET)
		var humidity := payload.decode_float(HUMIDITY_OFFSET)

		%StateSensor.text = "%.1f °C | %.1f %%" % [temperature, humidity]

	# -- MESSAGE LED --
	if subject_name == SUB_STATUS_LED_COLOR:
		if payload.size() != LED_COLOR_PAYLOAD_SIZE:
			return

		var received_color := Color.from_rgba8(
			payload.decode_u8(LED_RED_OFFSET), # Rouge
			payload.decode_u8(LED_GREEN_OFFSET), # Vert
			payload.decode_u8(LED_BLUE_OFFSET)  # Bleu
		)

		_color_led = received_color
		_update_led_color(received_color)
		_update_led_hex(received_color)
		
	if subject_name == SUB_STATUS_LED_STATE:
		if payload.size() != LED_STATE_PAYLOAD_SIZE:
			return

		_state_led = payload.decode_u8(LED_STATE_OFFSET) != 0
		_update_led_button()
		
# Affiche si le bouton physique est appuyé ou relâché.
func _set_button_state(is_pressed: bool) -> void:
	%DescStateButton.text = "PRESS" if is_pressed else "RELEASE"

	var color := BUTTON_PRESSED_COLOR if is_pressed else BUTTON_RELEASED_COLOR
	color.a = _state_button_style.bg_color.a
	_state_button_style.bg_color = color


# -- LED --

# Envoie la couleur choisie et attend la confirmation pour l’afficher.
func _send_led_color() -> void:
	var color: Color = %ColorPickerLED.color
	%ColorPickerLED.color = _color_led
	if not _connected: return
	
	
	var payload = PackedByteArray([
		color.r8,
		color.g8,
		color.b8
	])

	publish(SUB_SET_LED_COLOR, payload, true)
	
# Demande l’allumage ou l’extinction sans modifier directement l’affichage.
func _send_led_state() -> void:
	var requested_state := not _state_led
	var payload := PackedByteArray([int(requested_state)])

	publish(SUB_CMD_LED_STATE, payload, true)
	

# Affiche l’état de la LED confirmé par le capteur.
func _update_led_button() -> void:
	%TurnOffButtonLED.text = "TURN OFF" if _state_led else "TURN ON"

	var color := LED_ON_COLOR if _state_led else LED_OFF_COLOR
	color.a = _led_button_style.bg_color.a
	_led_button_style.bg_color = color

# Met à jour le sélecteur avec la couleur reçue du capteur.
func _update_led_color(color: Color) -> void:
	%ColorPickerLED.color = color

# Affiche la couleur reçue au format hexadécimal.
func _update_led_hex(color: Color) -> void:
	%HexaColorLED.text = "#" + color.to_html(false).to_upper()

	
# ---- Suivi de la connexion ----
# Anime le heartbeat et vérifie que le capteur répond toujours.
func _process(delta: float) -> void:
	_seconds_since_hb += delta
	%HeartbeatIcon.state = _seconds_since_hb < HB_BLINK_S
	if _connected and _seconds_since_hb >= HB_TIMEOUT_S:
		_connected = false  # plus de heartbeat : code du capteur bloqué ou capteur hors de portée


# Active la tuile et demande les états courants au capteur.
func _on_sensor_connected():
	%IDLabel.text = "node_id: %d" % _node_id
	%ConnectedIcon.state = true
	_update_availability()
	publish(SUB_GET_STATE, PackedByteArray())


# Grise les anciennes valeurs et désactive les commandes.
func _on_sensor_disconnected():
	%IDLabel.text = "node_id: ∅"
	%ConnectedIcon.state = false
	_update_availability()

## @brief Traite un heartbeat : (re)connecte le capteur et relance la temporisation.
## @param source_id  Identifiant de l'appareil émetteur.
func _on_hb(source_id: int):
	# Secours si la tuile n'a pas vu on_node_online (IHM lancée après le capteur, par exemple)
	if _node_id == UNKNOWN_NODE_ID: _node_id = source_id
	if source_id != _node_id: return
	_seconds_since_hb = 0.0
	_connected = true
	

# Active ou désactive les commandes selon la connexion du capteur.
func _update_availability() -> void:
	for control in [%ResetButton, %TurnOffButtonLED, %ColorPickerLED]:
		control.disabled = not _connected
	for control in [%CounterLabel, %StateButton, %StateSensor, %HexaColorLED, %ColorPickerLED, %TurnOffButtonLED]:
		control.modulate = Color.WHITE if _connected else STALE_MODULATE
	if not _connected:
		%ColorPickerLED.get_popup().hide()


# ---- Initialisation de la tuile ----
# Prépare les styles et relie les boutons à leurs commandes.
func _ready() -> void:
	super()
	
	# Applique un style propre à cet indicateur.
	_state_button_style = %StateButton.get_theme_stylebox("panel").duplicate() as StyleBoxFlat
	%StateButton.add_theme_stylebox_override("panel", _state_button_style)
	_set_button_state(false)
	
	
	%ResetButton.pressed.connect(
		func(): publish(SUB_CMD_RESET, PackedByteArray(), true)
	)
	
	
	
	_led_button_style = %TurnOffButtonLED.get_theme_stylebox("normal").duplicate() as StyleBoxFlat

	for style_name in ["normal", "hover", "pressed", "hover_pressed"]:
		%TurnOffButtonLED.add_theme_stylebox_override(
			style_name, _led_button_style
		)

	

	_update_led_button()
	


	%ColorPickerLED.edit_alpha = false

	%TurnOffButtonLED.pressed.connect(_send_led_state)
	%ColorPickerLED.popup_closed.connect(_send_led_color)
	_update_availability()
