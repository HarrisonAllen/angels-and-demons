#include <pebble.h>

// Uncomment for demo mode
// #define DEMO_MODE

#define UI_COLOR GColorWhite
#define NUM_WEATHER_ICONS 6 
#define MAX_CHARS 40
#define SETTINGS_KEY 1
#define WEATHER_CHECK_RATE 15
#define TEMP_MIDDLE 65
#define TEMP_RANGE 35
#define TEMP_MIN (TEMP_MIDDLE - TEMP_RANGE)
#define TEMP_MAX (TEMP_MIDDLE + TEMP_RANGE)
#define BATTERY_WIDTH 100

#define Y_OFFSET (PBL_DISPLAY_HEIGHT - 180) / 2
#define X_OFFSET (PBL_DISPLAY_WIDTH - 180) / 2
#define TEMP_LEFT_START GPoint(43, 126)
#define TEMP_LEFT_END GPoint(90, 120)
#define TEMP_RIGHT_START GPoint(90, 120)
#define TEMP_RIGHT_END GPoint(136, 126)
#define TEMP_SLIDER_CENTER GPoint(5, 8)


static Window *s_main_window;

static TextLayer *s_time_layer, *s_date_layer;
static BitmapLayer *s_entity_layer, *s_wings_layer, *s_temp_bar_layer,
  *s_temp_slider_layer, *s_weather_layer, *s_battery_layer;

static GFont s_time_font, s_date_font;
static GBitmap *s_entity_bitmap, *s_wings_bitmap, *s_temp_bar_bitmap,
  *s_temp_slider_bitmap, *s_weather_bitmap, *s_battery_bitmap, *s_charging_bitmap;

static uint8_t s_battery_level;
static bool s_charging, s_weather_loaded;
static int s_weather_minutes_elapsed;

typedef enum weather {
  SUNNY,
  PARTLYCLOUDY,
  CLOUDY,
  RAINY,
  SNOWY,
  STORMY
} Weather;

// Define settings struct
typedef struct ClaySettings {
  int TEMPERATURE;                   // Current temperature
  Weather CONDITIONS;                // Current weather conditions
  char OpenWeatherAPIKey[MAX_CHARS]; // API key for open weather
  bool AmericanDate;                 // use American date format (Jan 01)?
} ClaySettings;

static ClaySettings settings;

static const uint32_t WEATHER_ICONS[] = {
  RESOURCE_ID_IMAGE_W_SUN,  
  RESOURCE_ID_IMAGE_W_PART_SUN,
  RESOURCE_ID_IMAGE_W_CLOUD,
  RESOURCE_ID_IMAGE_W_RAIN,
  RESOURCE_ID_IMAGE_W_SNOW,
  RESOURCE_ID_IMAGE_W_STORM
};

#ifdef DEMO_MODE
// For manual demo
#define DEMO_BATTERY 100
#define DEMO_CHARGING false
#define DEMO_TEMPERATURE 65
#define DEMO_CONDITIONS SUNNY
#define DEMO_BLUETOOTH true
#define DEMO_TIME "50:05"
#define DEMO_DATE "Wed May 30"
#define DEMO_DAY "Wed"

// For using the cycles below
#define DEMO_CYCLE true
#define DEMO_CYCLE_POS 6

static char s_demo_times[][8] = {
  "05:33",
  "8:22",
  "14:01",
  "20:00",
  "11:59",
  "03:15",
  "1:11"
};

static char s_demo_dates[][12] = {
  "Sun Mar 22",
  "Mon Jul 05",
  "Tue Dec 31",
  "Wed 18 Feb",
  "Thu Nov 28",
  "Fri Jan 01",
  "Sat 15 Aug"
};

static bool s_demo_bluetooth[] = {
  true,
  true,
  true,
  false,
  true,
  true,
  true
};

static uint8_t s_demo_battery[] = {
  80,
  50,
  10,
  100,
  60,
  30,
  90
};

static bool s_demo_charging[] = {
  false,
  false,
  false,
  false,
  true,
  true,
  false
};

static Weather s_demo_weather[] = {
  SUNNY,
  PARTLYCLOUDY,
  CLOUDY,
  STORMY,
  RAINY,
  SNOWY,
  PARTLYCLOUDY
};

static int s_demo_temperature[] = {
  75,
  100,
  60,
  40,
  90,
  0,
  80
};
#endif

static void update_date();

// lerp between points a and b with percent c
static int lerp(int a, int b, float c){
  return (int)(a * (1.0 - c) + b * c);
}

static GPoint gpoint_lerp(GPoint a, GPoint b, float c) {
  return GPoint(
    lerp(a.x, b.x, c),
    lerp(a.y, b.y, c)
  );
}

// unlerp between points a and b with value c
static float unlerp(int a, int b, int c){
  int b_n = b - a; // normalized b
  int c_n = c - a; // normalized c
  return ((float)c_n) / ((float)(b_n));
}

// update the batter display layer
static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  s_charging = state.is_charging;
  
#ifdef DEMO_MODE
  uint8_t cur_battery = DEMO_CYCLE ? s_demo_battery[DEMO_CYCLE_POS] : DEMO_BATTERY;
  bool charging = DEMO_CYCLE ? s_demo_charging[DEMO_CYCLE_POS] : DEMO_CHARGING;
#else
  uint8_t cur_battery = s_battery_level;
  bool charging =  s_charging;
#endif
  bitmap_layer_set_bitmap(s_battery_layer, charging ? s_charging_bitmap : s_battery_bitmap);

  GRect cur_bounds = layer_get_bounds(bitmap_layer_get_layer(s_battery_layer));
  // Note that I can only do the following because coincidentally the width is 100
  GRect new_bounds = GRect(cur_bounds.origin.x, cur_bounds.origin.y, BATTERY_WIDTH - (100 - cur_battery), cur_bounds.size.h);
  layer_set_bounds(bitmap_layer_get_layer(s_battery_layer), new_bounds);
}

#if defined(PBL_BW)
static void dither(Layer *layer, GContext *ctx) {
  // Dither it up
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  GRect frame = layer_get_frame(layer);
  for (uint16_t y = frame.origin.y; y < frame.origin.y + frame.size.h; y++) {
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    for (uint16_t x = frame.origin.x; x < frame.origin.x + frame.size.w; x++) {
      uint8_t pixel_color = ((x + (y & 1)) & 1);
      uint16_t byte = (x >> 3); // x / 8
      uint8_t bit = x & 7; // x % 8
      uint8_t *byte_mod = &info.data[byte];
      if (!(*byte_mod & (1 << bit))) {
        continue;
      }
      *byte_mod ^= (-pixel_color ^ *byte_mod) & (1 << bit);
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}
#endif

// bluetooth status change
static void bluetooth_callback(bool connected) {
#ifdef DEMO_MODE
  connected = (DEMO_CYCLE ? s_demo_bluetooth[DEMO_CYCLE_POS] : DEMO_BLUETOOTH);
#endif
  layer_set_hidden(bitmap_layer_get_layer(s_entity_layer), !connected);
}

// default clay settings
static void default_settings() {  
  strcpy(settings.OpenWeatherAPIKey, ""); // Blank Key
  settings.TEMPERATURE = 65; // average temperature
  settings.CONDITIONS = SUNNY;     // average weather
  settings.AmericanDate = true;           // Jan 01 by default
}

// update display after reading from clay/weather
static void update_display() {
  // redraw the temperature
#ifdef DEMO_MODE
  int temperature = DEMO_CYCLE ? s_demo_temperature[DEMO_CYCLE_POS] : DEMO_TEMPERATURE;
#else
  int temperature = settings.TEMPERATURE;
#endif
  int rel_temp;
  GPoint start, end;
  if (temperature > TEMP_MAX) {
    temperature = TEMP_MAX;
  } else if (temperature < TEMP_MIN) {
    temperature = TEMP_MIN;
  }
  if (temperature > TEMP_MIDDLE) {
    rel_temp = temperature - TEMP_MIDDLE;
    start = TEMP_RIGHT_START;
    end = TEMP_RIGHT_END;
  } else {
    rel_temp = temperature - TEMP_MIN;
    start = TEMP_LEFT_START;
    end = TEMP_LEFT_END;
  }

  GPoint bar_pos = gpoint_lerp(start, end, (rel_temp / (float)TEMP_RANGE));
  GPoint slider_pos = GPoint(bar_pos.x - TEMP_SLIDER_CENTER.x, bar_pos.y - TEMP_SLIDER_CENTER.y);

  GRect cur_bounds = layer_get_bounds(bitmap_layer_get_layer(s_temp_slider_layer));
  GRect new_bounds = GRect(slider_pos.x + X_OFFSET, slider_pos.y + Y_OFFSET, cur_bounds.size.w, cur_bounds.size.h);
  layer_set_frame(bitmap_layer_get_layer(s_temp_slider_layer), new_bounds);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "%d < %d > %d, (%d, %d), %d", start.x, rel_temp, end.x, bar_pos.x, bar_pos.y, (int)((rel_temp / (float)TEMP_MIDDLE)*100));
  APP_LOG(APP_LOG_LEVEL_DEBUG, "(%d, %d), (%d, %d)", new_bounds.origin.x, new_bounds.origin.y, new_bounds.size.w, new_bounds.size.h);

  // update the weather icon
  gbitmap_destroy(s_weather_bitmap);
#ifdef DEMO_MODE
  s_weather_bitmap = gbitmap_create_with_resource(WEATHER_ICONS[DEMO_CYCLE ? s_demo_weather[DEMO_CYCLE_POS] : DEMO_CONDITIONS]);
#else
  s_weather_bitmap = gbitmap_create_with_resource(WEATHER_ICONS[settings.CONDITIONS]);
#endif
  bitmap_layer_set_bitmap(s_weather_layer, s_weather_bitmap);
}

// Read settings from persistent storage
static void load_settings() {
  // Load the default settings
  default_settings();
  // Read settings from persistent storage, if they exist
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

// Save the settings to persistent storage
static void save_settings() {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
  // Update the display based on new settings
  update_display();
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  update_date(tick_time);
}

static void request_weather() {
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);

  if (result == APP_MSG_OK) {
    // tell the app whether to use current location, celsius, and also the lat and lon
    dict_write_cstring(iter, MESSAGE_KEY_OpenWeatherAPIKey, settings.OpenWeatherAPIKey);

    // Send the message
    result = app_message_outbox_send();
  }
}

// Received data! Either for weather or settings
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  // Open Weather API Key
  Tuple *open_weather_api_key_t = dict_find(iterator, MESSAGE_KEY_OpenWeatherAPIKey);
  if(open_weather_api_key_t) {
    strcpy(settings.OpenWeatherAPIKey,open_weather_api_key_t->value->cstring);
  }

  // Current temperature and weather conditions
  Tuple *temp_tuple = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
  Tuple *conditions_tuple = dict_find(iterator, MESSAGE_KEY_CONDITIONS);

  // if weather data is available, use it
  if (temp_tuple && conditions_tuple) {
    settings.TEMPERATURE = (int)temp_tuple->value->int32;
    settings.CONDITIONS = (Weather)conditions_tuple->value->int32;
    s_weather_loaded = true;
  } else { // we weren't given weather, so either settings were updated or we were poked. Request it now
    request_weather();
  }

  // American date format?
  Tuple *american_date_t = dict_find(iterator, MESSAGE_KEY_AmericanDate);
  if(american_date_t) {
    settings.AmericanDate = american_date_t->value->int32 == 1;
  }

  save_settings(); // save the new settings! Current weather included
}

// Message failed to receive
static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

// Message failed to send
static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

// Message sent successfully
static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

// setup the display
static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Watchface Resources
  // wings
  s_wings_layer = bitmap_layer_create(GRect(0 + X_OFFSET, 0 + Y_OFFSET, 180, 180));
  s_wings_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WINGS);
  bitmap_layer_set_bitmap(s_wings_layer, s_wings_bitmap);
  
  // time
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_TIME_42));
  s_time_layer = text_layer_create(GRect(0, 32 + Y_OFFSET, bounds.size.w, 48));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, UI_COLOR);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  
  // date
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_20));
  s_date_layer = text_layer_create(GRect(0, 75 + Y_OFFSET, bounds.size.w, 26));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, UI_COLOR);
  text_layer_set_font(s_date_layer, s_date_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);

  // entity
  s_entity_layer = bitmap_layer_create(GRect(85 + X_OFFSET, 11 + Y_OFFSET, 10, 20));
  s_entity_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ENTITY);
  bitmap_layer_set_bitmap(s_entity_layer, s_entity_bitmap);
  bluetooth_callback(connection_service_peek_pebble_app_connection());


  // battery
  s_battery_layer = bitmap_layer_create(GRect(41 + X_OFFSET, 102 + Y_OFFSET, 100, 15));
  s_battery_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY_METER);
  s_charging_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CHARGING_METER);
  bitmap_layer_set_bitmap(s_battery_layer, s_battery_bitmap);

  // weather icon
  s_weather_layer = bitmap_layer_create(GRect(64 + X_OFFSET, 131 + Y_OFFSET, 52, 40));
  s_weather_bitmap = gbitmap_create_with_resource(WEATHER_ICONS[SUNNY]);
  bitmap_layer_set_bitmap(s_weather_layer, s_weather_bitmap);

  // temperature
  s_temp_bar_layer = bitmap_layer_create(GRect(43 + X_OFFSET, 120 + Y_OFFSET, 94, 8));
  s_temp_bar_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TEMP_BAR);
  bitmap_layer_set_bitmap(s_temp_bar_layer, s_temp_bar_bitmap);
  s_temp_slider_layer = bitmap_layer_create(GRect(85 + X_OFFSET, 112 + Y_OFFSET, 10, 16));
  s_temp_slider_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TEMP_SLIDER);
  bitmap_layer_set_bitmap(s_temp_slider_layer, s_temp_slider_bitmap);

  layer_add_child(window_layer, bitmap_layer_get_layer(s_wings_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_entity_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_battery_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_temp_bar_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_temp_slider_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_weather_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));

  update_display(); // and update the display to fill in everything
}

// unload everything!
static void main_window_unload(Window *window) {
  // unload text layers
  if (s_time_layer != NULL)
    text_layer_destroy(s_time_layer);
  if (s_date_layer != NULL)
    text_layer_destroy(s_date_layer);

  // unload custom fonts
  if (s_time_font != NULL)
    fonts_unload_custom_font(s_time_font);
  if (s_date_font != NULL)
    fonts_unload_custom_font(s_date_font);

  // unload bitmap layers
  if (s_wings_layer != NULL)
    bitmap_layer_destroy(s_wings_layer);
  if (s_entity_layer != NULL)
    bitmap_layer_destroy(s_entity_layer);
  if (s_battery_layer != NULL)
    bitmap_layer_destroy(s_battery_layer);
  if (s_temp_bar_layer != NULL)
    bitmap_layer_destroy(s_temp_bar_layer);
  if (s_temp_slider_layer != NULL)
  bitmap_layer_destroy(s_temp_slider_layer);
  if (s_weather_layer != NULL)
    bitmap_layer_destroy(s_weather_layer);

  // unload gbitmaps
  if (s_wings_bitmap != NULL)
    gbitmap_destroy(s_wings_bitmap);
  if (s_entity_bitmap != NULL)
    gbitmap_destroy(s_entity_bitmap);
  if (s_battery_bitmap != NULL)
    gbitmap_destroy(s_battery_bitmap);
  if (s_temp_bar_bitmap != NULL)
    gbitmap_destroy(s_temp_bar_bitmap);
  if (s_temp_slider_bitmap != NULL)
    gbitmap_destroy(s_temp_slider_bitmap);
  if (s_weather_bitmap != NULL)
    gbitmap_destroy(s_weather_bitmap);
}

// update the time display
static void update_time() {
#ifdef DEMO_MODE
  text_layer_set_text(s_time_layer, DEMO_CYCLE ? s_demo_times[DEMO_CYCLE_POS] : DEMO_TIME);
#else
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  // put hours and minutes into buffer
  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ?
                                        "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer); 
#endif
}

static void update_date(struct tm *tick_time){
#ifdef DEMO_MODE
  if (DEMO_CYCLE) {
    text_layer_set_text(s_date_layer, s_demo_dates[DEMO_CYCLE_POS]);
  } else {
    text_layer_set_text(s_date_layer, DEMO_DATE);
  }
#else
  static char s_date_buffer[12];
  
  if (settings.AmericanDate) {
    strftime(s_date_buffer, sizeof(s_date_buffer), "%a %b %d", tick_time);
  } else {
    strftime(s_date_buffer, sizeof(s_date_buffer), "%a %d %b", tick_time);
  }
  text_layer_set_text(s_date_layer, s_date_buffer);
#endif
}

// this fires every minute
static void tick_handler(struct tm *tick_time, TimeUnits units_changes) {
  update_time();
  update_date(tick_time);

  // update the weather
  s_weather_minutes_elapsed++;
  if (s_weather_minutes_elapsed >= WEATHER_CHECK_RATE) { // time to check the weather
    // get the weather
    request_weather();
    s_weather_minutes_elapsed = 0;
  } 

}

// classic init, you know we need it
static void init() {
  load_settings();

  // setup window
  s_main_window = window_create();

  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  window_stack_push(s_main_window, true);

  // set up tick_handler to run every minute
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  // want to display time and date at the start
  update_time();
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  update_date(tick_time);

  // callback for battery level updates
  battery_state_service_subscribe(battery_callback);
  // display battery at the start
  battery_callback(battery_state_service_peek());

  // callback for bluetooth connection updates
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bluetooth_callback
  });

  // Register callbacks for settings/weather updates
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);

  // Open AppMessage
  const int inbox_size = 1024; // maaaaybe overkill, but 128 isn't enough
  const int outbox_size = 1024;
  app_message_open(inbox_size, outbox_size);
}

// classic deinit, you know we need this too
static void deinit() {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
