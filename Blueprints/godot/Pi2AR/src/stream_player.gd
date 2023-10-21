extends Control

var socket = WebSocketPeer.new()
var frame = Image.new()
var meas_fps_start = Time.get_ticks_msec()
var fps_counter : int = 0
var time_elapsed : float = 0
var loading_thread = Thread.new()
var mutex_decoding_process = Mutex.new()
var semaphore_data_ready = Semaphore.new()
var image_data: PackedByteArray

func _ready():
	socket.set_inbound_buffer_size(WS_CONF.INBOUND_BUFFER_SIZE)
	_handle_connect()
	loading_thread.start(_load_image)

func _process(_delta):
	socket.poll()
	var state = socket.get_ready_state()
	if state == WebSocketPeer.STATE_OPEN: # if connected
		while socket.get_available_packet_count() > 0: # if receiving packets
			_handle_stream(socket.get_packet()) # handle image data
					
	elif state == WebSocketPeer.STATE_CLOSING:
		# Keep polling to achieve proper close.
		pass
	elif state == WebSocketPeer.STATE_CLOSED:
		var code = socket.get_close_code()
		var reason = socket.get_close_reason()
		print_to_debug("WebSocket closed with code: %d, reason %s. Clean: %s" % [code, reason, code != -1])
		set_process(false) # Stop processing
		_handle_connect()

func print_to_debug(msg : String) -> void:
	WS_CONF.DEBUG = msg + "\n"
	print(msg)

func _handle_connect() -> void:
	await get_tree().create_timer(WS_CONF.RECONNECT_WAIT).timeout
	var state = socket.get_ready_state()
	if state == WebSocketPeer.STATE_CLOSED:
		if socket.connect_to_url(WS_CONF.WS_URI) == OK:
			print_to_debug("Connected to %s" % [WS_CONF.WS_URI])
			set_process(true) # Keep the connection open
	else:
		print_to_debug("Failed to connect to %s" % [WS_CONF.WS_URI])

func _handle_stream(data: PackedByteArray) -> void:
	if mutex_decoding_process.try_lock():  # Check if decoding is finished
		#loading_thread.call("_load_image", data)
		# copy data
		image_data = data.duplicate() # this might take time
		semaphore_data_ready.post() # signal that data is ready
		mutex_decoding_process.unlock()  # allow access to data
	#else:
		## If mutex is locked, skip the frame
		#print_to_debug("Skipped a frame as still processing the previous one")

func _load_image() -> void:
	while (true):
		semaphore_data_ready.wait()  # wait until some data is ready
		mutex_decoding_process.lock() # get access to data
		#var frame = Image.new()
		var error = frame.load_jpg_from_buffer(image_data)
		mutex_decoding_process.unlock() # signal that encoding is ready
		if error == OK:
			_update_texture.call_deferred() # needs to be updated later - in ui thread?
			calc_FPS()
		#else: # TODO: consider counting errors here
			#print_to_debug("Failed to load received image, error code %s" % [error])

func _update_texture() -> void:
	$Margin/Stream.texture = ImageTexture.create_from_image(frame)

func calc_FPS() -> void:	
	fps_counter += 1
	time_elapsed = Time.get_ticks_msec() - meas_fps_start
	if time_elapsed > WS_CONF.FPS_UPDATE_DELAY:
		WS_CONF.FPS = str(snapped(fps_counter/(time_elapsed/1000.0), 1))
		fps_counter = 0
		meas_fps_start = Time.get_ticks_msec()
