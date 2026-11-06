extends Tile

# ---- CONSTANTES ----
const DEVICE_NAME = "sensor_i"
const SUB_HB = "hb_" + DEVICE_NAME
## Délai sans heartbeat au-delà duquel le capteur est considéré comme bloqué (3,5 périodes de 1 s).
const HB_TIMEOUT_S := 3.5
## Durée d'allumage de l'icône de heartbeat à chaque réception.
const HB_BLINK_S := 0.5

const SUB_STATUS_COUNTER = "status_" + DEVICE_NAME + "_counter"
const SUB_CMD_RESET = "cmd_" + DEVICE_NAME + "_reset"
const SUB_STATUS_BUTTON = "status_" + DEVICE_NAME + "_button"
const SUB_STATUS_SENSOR = "status_" + DEVICE_NAME + "_sensor"

const SUB_CMD_LED_COLOR = "cmd_" + DEVICE_NAME + "_led_color"
const SUB_STATUS_LED_COLOR = "status_" + DEVICE_NAME + "_led_color"

const SUB_CMD_LED_STATE = "cmd_" + DEVICE_NAME + "_led_state"
const SUB_STATUS_LED_STATE = "status_" + DEVICE_NAME + "_led_state"

# ---- VARIABLES ----
var _node_id: int = -1
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
	


func on_node_offline(node_id: int, _reason_code: int) -> void:
	if node_id == _node_id: _connected = false


func on_gateway_reachability_changed(is_gateway_reachable: bool) -> void:
	if is_gateway_reachable: return
	_connected = false
	_node_id = -1  # la passerelle peut redistribuer les identifiants en redémarrant


func on_message(subject_name: String, source_id: int, payload: PackedByteArray) -> void:
	if subject_name == SUB_HB: _on_hb(source_id)
	if source_id != _node_id: return # ne traitez que les messages dédiés à votre capteur
	if subject_name == SUB_STATUS_COUNTER:
		if payload.size() != 4: return # le contrat : 4 octets, sinon on ignore
		%CounterLabel.text = str(payload.decode_u32(0))
		
		
	if subject_name == SUB_STATUS_BUTTON:
		if payload.size() != 1: return
		_set_button_state(bool(payload.decode_u8(0)))	



	if subject_name == SUB_STATUS_SENSOR:
		if payload.size() != 8:
			return
		var temperature := payload.decode_float(0)
		var humidity := payload.decode_float(4)

		%StateSensor.text = "%.1f °C | %.1f %%" % [temperature, humidity]

	# -- MESSAGE LED --
	if subject_name == SUB_STATUS_LED_COLOR:
		if payload.size() != 3:
			return

		var received_color := Color.from_rgba8(
			payload.decode_u8(0), # Rouge
			payload.decode_u8(1), # Vert
			payload.decode_u8(2)  # Bleu
		)

		_color_led = received_color
		_update_led_color(received_color)
		_update_led_hex(received_color)
		
	if subject_name == SUB_STATUS_LED_STATE:
		if payload.size() != 1:
			return

		_state_led = payload.decode_u8(0) != 0
		_update_led_button()
		
func _set_button_state(is_pressed: bool) -> void:
	%DescStateButton.text = "PRESS" if is_pressed else "RELEASE"

	var color := Color(0x00d67bff) if is_pressed else Color(0x0090d5ff)
	color.a = _state_button_style.bg_color.a
	_state_button_style.bg_color = color


# -- LED --

func _send_led_color() -> void:
	var color: Color = %ColorPickerLED.color
	
	
	var payload = PackedByteArray([
		color.r8,
		color.g8,
		color.b8
	])

	publish(SUB_CMD_LED_COLOR, payload, true)
	
func _send_led_state() -> void:
	var requested_state := not _state_led
	var payload := PackedByteArray([int(requested_state)])

	publish(SUB_CMD_LED_STATE, payload, true)
	

func _update_led_button() -> void:
	%TurnOffButtonLED.text = "TURN OFF" if _state_led else "TURN ON"

	var color := Color(0xff2147ff) if _state_led else Color(0x00d67bff)
	color.a = _led_button_style.bg_color.a
	_led_button_style.bg_color = color

func _update_led_color(color: Color) -> void:
	%ColorPickerLED.color = color

func _update_led_hex(color: Color) -> void:
	%HexaColorLED.text = "#" + color.to_html(false).to_upper()

	
func _process(delta: float) -> void:
	_seconds_since_hb += delta
	%HeartbeatIcon.state = _seconds_since_hb < HB_BLINK_S
	if _connected and _seconds_since_hb >= HB_TIMEOUT_S:
		_connected = false  # plus de heartbeat : code du capteur bloqué ou capteur hors de portée


func _on_sensor_connected():
	%IDLabel.text = "node_id: %d" % _node_id
	%ConnectedIcon.state = true


func _on_sensor_disconnected():
	%IDLabel.text = "node_id: ∅"
	%ConnectedIcon.state = false

## @brief Traite un heartbeat : (re)connecte le capteur et relance la temporisation.
## @param source_id  Identifiant de l'appareil émetteur.
func _on_hb(source_id: int):
	# Secours si la tuile n'a pas vu on_node_online (IHM lancée après le capteur, par exemple)
	if _node_id == -1: _node_id = source_id
	if source_id != _node_id: return
	_seconds_since_hb = 0.0
	_connected = true
	

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
	
