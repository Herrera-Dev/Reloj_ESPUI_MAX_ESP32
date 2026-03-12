
// -------------------------------------------------------
#define WEB 1       // EspUI Web
#define MOD_RTC 0   // Modulo RTC DS3231
#define MOD_DHT 1   // Modulo DHT22
#define MOD_MICRO 1 // Modulo de sonido KY-037
#define OTA 1       // Habilitar OTA
#define ENCODER 0   // Habilitar encoder para el temporizador

// -------------------------------------------------------
#include "esp_system.h"
#include "SPIFFS.h"
#if OTA
#include <ArduinoOTA.h>
#include "config_OTA.hpp"
#endif

#if WEB
#include <ESPUI.h>
#endif
#include <ArduinoJson.h>

#include "time.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Wire.h>

#if MOD_RTC
#include "RTClib.h"
#endif
#include "SPI.h"
#include <MD_Parola.h>
#if MOD_DHT
#include <DHT.h>
#endif
#include <Preferences.h>
Preferences mem;

#include "fontText.h"
#include "pixel_Art.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 5
#define DATA_PIN 23 // or MOSI
#define CS_PIN 5    // or SS (cambiable a otro gpio)
#define CLK_PIN 18  // or SCK

MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
// Edit font: Grados --> &, Espacio --> _

const uint8_t CALIB_VELEFFECT = 4; // Calibrar velocidad de todos los efectos de entrada y salida
const int UPDTIME_OWM = 300000;    // 5 min. -> actualizar api OpenWeatherMap (Plan free)
const int UPDTIME_CMC = 300000;    // 5 min. -> actualizar api CoinMarketCap (Plan free)
const int UPDTIME_SA = 300000;     // 5 min. -> actualizar api SerpApi (Plan free)

IPAddress miIP(192, 168, 6, 1);
const char *mdns = "espui";             // Web: http://espui.local/
const char *hostname = "ESPUI-MAX";     // SSID y mDNS
const char *hostpass = "12345678";      // Password: WiFi AP y Web Server
const char *ntpServer = "pool.ntp.org"; // Time NTP

// ----------------------------------------------------
bool memModAP = false;  // NVC WiFi AP o STA
bool memModOTA = false; // NVC OTA Update

uint8_t tMsjWeb = 4; // Tiempo de las alertas en espui
String cabInfo = ""; // Mensaje de las alertas en espui

bool tipMsj;
bool memEncendido = true;     // NVC Encendido o Apagado
bool memClimaOWM = true;      // NVC 0=DHT22, 1=openWheaterMap
bool memAnimAle = true;       // NVC Aleatorio=1, No aleatorio=0
uint8_t memAnimEfecto = 1;    // NVC Select: efecto
uint8_t memAnimDuracion = 10; // NVC Duracion: minutos

struct hora
{
  uint8_t h;
  uint8_t m;
  uint8_t s;
  bool est;
};

struct tiempo
{
  uint8_t hora;   // 0 a 23
  uint8_t minuto; // 0 a 59
  uint8_t dia;    // 1 a 31
  uint8_t mes;    // 1 a 12
  bool estado;
};

struct tipoEfecto
{
  const char *nombre;
  const textEffect_t efecto;
  uint16_t veloc;
};
tipoEfecto catalogo[] = {
    {"PRINT", PA_PRINT, 1},
    {"SPRITE", PA_SPRITE, 6},
    {"SCROLL_U", PA_SCROLL_UP, 5},
    {"SCROLL_L", PA_SCROLL_LEFT, 7},
    {"MESH", PA_MESH, 20},
    {"FADE", PA_FADE, 20},
    {"RANDOM", PA_RANDOM, 3},
    {"WIPE_C", PA_WIPE_CURSOR, 4},
    {"SCAN_H", PA_SCAN_HORIZ, 4},
    {"SCAN_VX", PA_SCAN_VERTX, 3},
    {"OPENING_C", PA_OPENING_CURSOR, 4},
    {"CLOSING", PA_CLOSING, 3},
    {"SCROLL_UL", PA_SCROLL_UP_LEFT, 7},
    {"SCROLL_DL", PA_SCROLL_DOWN_LEFT, 7},
    {"GROW_U", PA_GROW_UP, 7},
};

struct creden
{
  char ssid[15];
  char password[20];
  char keyCripto[45];  // API CoinMarketCap --> https://pro.coinmarketcap.com/
  char criptos[2][6];  // Ejm: BTC, ETH
  char divisa[6];      // Ejm: USD, EUR, ARS, MXN
  char keyWeather[40]; // API openWeatherMap --> https://home.openweathermap.org/users/sign_in
  char city[27];       // Ejm: New York,US - London,GB - Madrid,ES --> supported location: https://openweathermap.org/
  char equipo[50];     // Ejm: Futbol Club Barcelona, Real Madrid Club de Fútbol
  char location[30];   // Ejm: Mexico City,Mexico City,Mexico | New York,New York,United States --> supported location: https://serpapi.com/locations-api
  char api_key[2][70];
};
creden miLlave = {"vacio", "vacio", "vacio", {"BTC", "ETH"}, "USD", "vacio", "London,GB", "vacio", "Mexico City,Mexico", {"vacio", "vacio"}};
tiempo miTiempo = {0, 0, 1, 1, false}; // Reloj

// SERPAPI FUTBOL
struct traducion
{
  const char *fuent;
  const char *translate;
};
const traducion dic[] = { // Traducir EN -> ESP
    {"today", "Hoy"},
    {"tomorrow", "Man"},
    {"mon", "Lun"},
    {"tue", "Mar"},
    {"wed", "Mie"},
    {"thu", "Jue"},
    {"fri", "Vie"},
    {"sat", "Sab"},
    {"sun", "Dom"}};

enum partidoEst : uint8_t
{
  FX,
  LIVE,
  HT,
  FT,
  ET,
  Pens,
  PP,
  Abd,
  vacio,
  error
};
struct futbol
{
  partidoEst estado;
  char torneo[30];
  char local[30];
  char sLocal[7];
  char visita[30];
  char sVisita[7];
  uint8_t min;
  uint8_t minExt;
  char dia[10];        // "today", "tomorrow", "Wed"
  uint8_t fechaDia;    // 1-31
  uint8_t fechaMes;    // 1-12
  uint8_t fechaHora;   // "12"
  uint8_t fechaMinuto; // "45"
};
futbol fxInfo;
uint8_t countSA = 0;
uint8_t restSA = 250;
uint8_t velocSA = 0;
bool flagSA = false;

// MICROFONO
#if MOD_MICRO
#define micro 35
#endif

// BUZZER
#define buzzer 33
bool sonando = false;

// DHT22
#if MOD_DHT
#define sensor_temp 15
#define Tipo DHT22
DHT dht(sensor_temp, Tipo);
const float CALIB_TEMP = -0.60; // Mod. DHT22
#endif

// RELOJ
#if MOD_RTC
RTC_DS3231 rtc;
#endif
bool is12h = false;      // formato 12H o 24H
int8_t memHours = -4;    // desfase UTC -4, -3, etc.
int8_t memDaylight = 0;  // hora de verano
bool memRelojUDP = true; // NVC 0=RTC, 1=UDP

uint8_t minAnt = 0;
bool rSeg = true;
bool runReloj = false;
uint8_t posiReloj = 0;

// ENCODER
#if ENCODER
#define A_CLK 19
#define B_DT 4
#define BTN_SW 17
int lastCLKState;
int currCLKState;

hora miTimer = {0, 0, 0, false};
uint8_t posiTempo = 0;
bool modoTempo = false;
unsigned long duracion = 0;
unsigned long tiempoInicio = 0;
#endif

// DISPLAY
enum vistas : uint8_t
{
  vReloj,
  vTemp,
  vHum,
  vCript1,
  vCript2,
  vFutbol,
  vTimer,
  vVacio
};
vistas modo;
byte miEfecto = 2;
bool fontSelect = false;

// Modos
struct modV
{
  char datos[9];
  uint8_t duracion;
  bool estado;
};
modV catVista[] = {
    {"0", 15, true}, // Reloj
    {"0", 15, true}, // Temperatura
    {"0", 15, true}  // Humedad
};

struct modV_CMC
{
  char datos[18];
  uint8_t veloc;
  bool estado;
};
modV_CMC catCrypto[] = {
    {"0", 20, true}, // Crypto 1
    {"0", 20, true}  // Crypto 2
};

struct modV_SA
{
  int veloc;
  int estado;
};
modV_SA catDeport[] = {
    {7, false} // Futbol
};

// Funciones
struct alrm
{
  hora hor;
  bool repetir;
  bool silenciar;
};
alrm miAlarma = {{0, 0, 0, false}, false, false};
bool mensAlarm = true;

struct mensaje
{
  bool estado;       // NVC activar o desactivar
  bool tipo;         // NVC Vertical=1, Horizontal=0
  bool indefi;       // NVC Indefinido=1, No Indefinido=0
  uint8_t duracion;  // NVC duracion en pantalla
  uint8_t velocidad; // NVC velocidad efecto
  uint8_t pausa;     // NVC pausar en pantalla
};
mensaje miMsj = {false, false, false, 2, 20, 0};
char miMensaje[50];

unsigned long tAntUpdate;
unsigned long tAntSegdr;
unsigned long tAntEfecto;
unsigned long tAntMensaj;
unsigned long tAntWebCab;
unsigned long tAntClimaUpd;
unsigned long tAntCriptUpd;

// ========================================
#if WEB
static const char deshabilitar[] = "background-color: #bbb; border-bottom: #999 3px solid;";
uint16_t cabEstado;
uint16_t horaEstado, horaTipo, climaTipo, horaDuracionR, horaDuracionT, horaDuracionH, horaBrillo, horaEst, tempEst, humeEst, crypto1, crypto2, deport1, criptoVel, futbolVel;
uint16_t animAleatorio, animEfecto, animDuracion, btnRTC, desfUTC, horVer;
uint16_t alarmRep, alarmHor, alarmGuard, alarmSilen;
uint16_t mensTipo, mensText, mensDura, mensVelo, mensPausa, mensGuard;
uint16_t ota, modAp, wifi, pass, wifiGuard, apiOWM, owmCode, owmGuard, apiCMC, cmcCripto1, cmcCripto2, cmcDivisa, cmcGuard;
uint16_t teamSA, locationSA, key1SA, key2SA, guardSA;

// ---------------------------------------
void setupESPUI()
{
  static const char estiloInfo[] = "background-color: unset; margin-bottom: 0px;"; // estado de info fondo y separacion
  static const char labelsPer[] = "background-color: unset; width: 100%;";         // sobre labels
  static const char inputColor[] = "color: rgba(0, 0, 0, 1); margin-bottom: .3rem;";

  uint16_t tab0 = ESPUI.addControl(Tab, "", "Vista");
  uint16_t tab1 = ESPUI.addControl(Tab, "", "Pantalla");
  uint16_t tab3 = ESPUI.addControl(Tab, "", "Efectos");
  uint16_t tab2 = ESPUI.addControl(Tab, "", "Alarma");
  uint16_t tab4 = ESPUI.addControl(Tab, "", "Texto");
  uint16_t tab5 = ESPUI.addControl(Tab, "", "APIs");
  uint16_t tab6 = ESPUI.addControl(Tab, "", "WiFi");

  // ESTADOS--------------------------------------
  static String x = miIP.toString();
  cabEstado = ESPUI.addControl(Label, x.c_str(), info(), Turquoise);
  ESPUI.addControl(Button, "Boton", "Reiniciar ESP32", None, cabEstado, espReiniciar);
  ESPUI.setElementStyle(cabEstado, estiloInfo);

  // VISTA-------------------------------------
  horaEst = ESPUI.addControl(Switcher, "Carrusel", String(catVista[0].estado), Peterriver, tab0, webReEst);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Reloj", None, horaEst), labelsPer);
  tempEst = ESPUI.addControl(Switcher, "", String(catVista[1].estado), Peterriver, horaEst, webTeEst);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Temperatura", None, horaEst), labelsPer);
  humeEst = ESPUI.addControl(Switcher, "", String(catVista[2].estado), Peterriver, horaEst, webHuEst);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Humedad", None, horaEst), labelsPer);
  crypto1 = ESPUI.addControl(Switcher, "", String(catCrypto[0].estado), Peterriver, horaEst, webCryptoEst1);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Cripto #1", None, horaEst), labelsPer);
  crypto2 = ESPUI.addControl(Switcher, "", String(catCrypto[1].estado), Peterriver, horaEst, webCryptoEst2);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Cripto #2", None, horaEst), labelsPer);
  deport1 = ESPUI.addControl(Switcher, "", String(catDeport[0].estado), Peterriver, horaEst, webDeportEst1);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Futbol", None, horaEst), labelsPer);

  horaDuracionR = ESPUI.addControl(Number, "Duracion", String(catVista[0].duracion), Dark, tab0, webRelojDuracion);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Reloj", None, horaDuracionR), labelsPer);
  horaDuracionT = ESPUI.addControl(Number, "", String(catVista[1].duracion), Dark, horaDuracionR, webRelojDuracion);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Temperatura", None, horaDuracionR), labelsPer);
  horaDuracionH = ESPUI.addControl(Number, "", String(catVista[2].duracion), Dark, horaDuracionR, webRelojDuracion);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Humedad", None, horaDuracionR), labelsPer);
  ESPUI.setElementStyle(horaDuracionR, inputColor);
  ESPUI.setElementStyle(horaDuracionT, inputColor);
  ESPUI.setElementStyle(horaDuracionH, inputColor);

  criptoVel = ESPUI.addControl(Slider, "Velocidad", String(catCrypto[0].veloc), Dark, tab0, webVelocCrip);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "+ Criptos -", None, criptoVel), labelsPer);
  ESPUI.addControl(Min, "", "10", None, criptoVel);
  ESPUI.addControl(Max, "", "150", None, criptoVel);
  futbolVel = ESPUI.addControl(Slider, "", String(velocSA), Dark, criptoVel, webVelocFutb);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "+ Futbol -", None, criptoVel), labelsPer);
  ESPUI.addControl(Min, "", "10", None, futbolVel);
  ESPUI.addControl(Max, "", "150", None, futbolVel);

  // PANTALLA -------------------------------------
  horaEstado = ESPUI.addControl(Switcher, "Encender", "1", Wetasphalt, tab1, webRelojEstado); // Estado Encendido o Apagado
  horaBrillo = ESPUI.addControl(Slider, "Brillo", "0", Dark, tab1, webRelojBrillo);
  ESPUI.addControl(Min, "", "0", None, horaBrillo);
  ESPUI.addControl(Max, "", "10", None, horaBrillo);

  climaTipo = ESPUI.addControl(Select, "Clima", "", Emerald, tab1, webClimaTipo); // Fuente de datos de temp. y hum.
  ESPUI.addControl(Option, "Modulo DHT22", "0", Alizarin, climaTipo);
  ESPUI.addControl(Option, "API openWeatherMap", "1", Alizarin, climaTipo);

  horaTipo = ESPUI.addControl(Select, "Hora", "", Emerald, tab1, webRelojTipo); // Tipo de reloj rtc o ntp
  ESPUI.addControl(Option, "Modulo RTC", "O", Alizarin, horaTipo);
  ESPUI.addControl(Option, "Servidor NTP", "1", Alizarin, horaTipo);
  ESPUI.addControl(Switcher, "Reloj formato 12H", String(is12h), Wetasphalt, tab1, webFort12h);

  desfUTC = ESPUI.addControl(Number, "Hora", String(memHours), Peterriver, tab1, webRelojDesf);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "UTC", None, desfUTC), labelsPer);
  horVer = ESPUI.addControl(Number, "Horario de verano", String(memDaylight), Peterriver, desfUTC, webRelojVeran);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Horario de Verano", None, desfUTC), labelsPer);
  ESPUI.setElementStyle(desfUTC, inputColor);
  ESPUI.setElementStyle(horVer, inputColor);

  btnRTC = ESPUI.addControl(Button, "Modulo RTC DS3231", "Actualizar Datos", Alizarin, tab1, webRelojGuardar); // Guardar nueva hora

  // ALARMA------------------------------------
  alarmHor = ESPUI.addControl(Text, "Hora", strHora(miAlarma.hor.h, miAlarma.hor.m), Peterriver, tab2, webAlarmHora); // Hora de alarma
  ESPUI.setInputType(alarmHor, "time");
  ESPUI.setElementStyle(alarmHor, inputColor);

  alarmRep = ESPUI.addControl(Switcher, "Repetir", String(miAlarma.repetir), Wetasphalt, tab2, webAlarmRepet); // Repetir alarma ON o OFF
  alarmSilen = ESPUI.addControl(Button, "Silenciar", "Aceptar", Emerald, tab2, webAlarmSilen);
  alarmGuard = ESPUI.addControl(Switcher, "Activar", String(miAlarma.hor.est), Alizarin, tab2, webAlarmGuard); // Guardar o cancelar la alarma

  // EFECTOS ---------------------------------
  animAleatorio = ESPUI.addControl(Switcher, "Efecto: Aleatorio", String(memAnimAle), Wetasphalt, tab3, webAnimAleatorio); // Aleatorio
  animEfecto = ESPUI.addControl(Select, "Efectos", "", Peterriver, tab3, webanimEfecto);
  for (static int i = 0; i < ARRAY_SIZE(catalogo); i++)
    ESPUI.addControl(Option, catalogo[i].nombre, String(i).c_str(), None, animEfecto);

  animDuracion = ESPUI.addControl(Slider, "Duracion (Min)", String(memAnimDuracion), Dark, tab3, webAnimDuracion);
  ESPUI.addControl(Min, "", "3", None, animDuracion);
  ESPUI.addControl(Max, "", "120", None, animDuracion);

  ESPUI.addControl(Switcher, "Simple <- Font -> Doble", String(fontSelect), Wetasphalt, tab3, webFont);

  // MENSAJE ------------------------------------
  mensText = ESPUI.addControl(Text, "Mensaje", "", Sunflower, tab4, webMensTexto);
  mensTipo = ESPUI.addControl(Select, "Orientacion", "", Emerald, tab4, webMensTipo);
  ESPUI.addControl(Option, "Horizontal", "O", Alizarin, mensTipo);
  ESPUI.addControl(Option, "Vertical", "1", Alizarin, mensTipo);

  ESPUI.addControl(Switcher, "Duracion Indefinido", String(miMsj.indefi), Wetasphalt, tab4, webMensIndefi);
  mensDura = ESPUI.addControl(Slider, "Duracion (Min)", "2", Dark, tab4, webMensDuracion);
  ESPUI.addControl(Min, "", "1", None, mensDura);
  ESPUI.addControl(Max, "", "60", None, mensDura);

  mensVelo = ESPUI.addControl(Slider, "Efecto", "20", Dark, tab4, webMensVelocidad);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "+ Velocidad (ms) -", None, mensVelo), labelsPer);
  ESPUI.addControl(Min, "", "10", None, mensVelo);
  ESPUI.addControl(Max, "", "150", None, mensVelo);

  mensPausa = ESPUI.addControl(Slider, "", "0", Dark, mensVelo, webMensPausa);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Pausa (Seg)", None, mensVelo), labelsPer);
  mensGuard = ESPUI.addControl(Button, "Mensaje", "Enviar", Alizarin, tab4, webMensEnviar);
  ESPUI.addControl(Min, "", "0", None, mensPausa);
  ESPUI.addControl(Max, "", "60", None, mensPausa);

  // CREDENCIALES ------------------------------------
  apiOWM = ESPUI.addControl(Text, "OpenWeatherMap", "", Peterriver, tab5, webApiOWM);
  ESPUI.setInputType(apiOWM, "password");
  ESPUI.setElementStyle(ESPUI.addControl(Label, "Api Key", "API Key", None, apiOWM), labelsPer);
  owmCode = ESPUI.addControl(Text, "Ciudad", String(miLlave.city), Dark, apiOWM, webOWMCode);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "City (London,GB)", None, apiOWM), labelsPer);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "<a href='https://openweathermap.org/' target='_blank' style='color:white;'>Mas inf. sobre city</a>", None, apiOWM), labelsPer);
  owmGuard = ESPUI.addControl(Button, "Nuevos OWM", "Guardar", Alizarin, apiOWM, webApiGuard);
  ESPUI.setElementStyle(apiOWM, inputColor);
  ESPUI.setElementStyle(owmCode, inputColor);

  apiCMC = ESPUI.addControl(Text, "CoinMarketCap", "", Turquoise, tab5, webApiCMC);
  ESPUI.setInputType(apiCMC, "password");
  ESPUI.setElementStyle(ESPUI.addControl(Label, "Api Key", "API Key", None, apiCMC), labelsPer);
  cmcCripto1 = ESPUI.addControl(Text, "Cripto 1", String(miLlave.criptos[0]), Dark, apiCMC, webCMCCripto1);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Cripto #1", None, apiCMC), labelsPer);
  cmcCripto2 = ESPUI.addControl(Text, "Cripto 2", String(miLlave.criptos[1]), Dark, apiCMC, webCMCCripto2);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Cripto #2", None, apiCMC, webCMCCripto2), labelsPer);
  cmcDivisa = ESPUI.addControl(Text, "Divisa", String(miLlave.divisa), Dark, apiCMC, webCMCDivisa);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Divisa", None, apiCMC), labelsPer);
  cmcGuard = ESPUI.addControl(Button, "Nuevos CMC", "Guardar", Alizarin, apiCMC, webApiGuard);
  ESPUI.setElementStyle(apiCMC, inputColor);
  ESPUI.setElementStyle(cmcCripto1, inputColor);
  ESPUI.setElementStyle(cmcCripto2, inputColor);
  ESPUI.setElementStyle(cmcDivisa, inputColor);

  key1SA = ESPUI.addControl(Text, "SerpApi", "", Peterriver, tab5, webApiSA1);
  ESPUI.setInputType(key1SA, "password");
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "API Key #1", None, key1SA), labelsPer);
  key2SA = ESPUI.addControl(Text, "SerpApi 2", "", Dark, key1SA, webApiSA2);
  ESPUI.setInputType(key2SA, "password");
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Api Key #2", None, key1SA), labelsPer);
  teamSA = ESPUI.addControl(Text, "Team", String(miLlave.equipo), Dark, key1SA, webSATeam);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Team", None, key1SA), labelsPer);
  locationSA = ESPUI.addControl(Text, "Location", String(miLlave.location), Dark, key1SA, webSALocation);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Location (Mexico City,Mexico)", None, key1SA), labelsPer);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "<a href='https://serpapi.com/locations-api' target='_blank' style='color:white;'>Mas inf. sobre location</a>", None, key1SA), labelsPer);
  guardSA = ESPUI.addControl(Button, "Nuevos SA", "Guardar", Peterriver, key1SA, webApiGuard);
  ESPUI.setElementStyle(key1SA, inputColor);
  ESPUI.setElementStyle(key2SA, inputColor);
  ESPUI.setElementStyle(teamSA, inputColor);
  ESPUI.setElementStyle(locationSA, inputColor);

  // WIFI ------------------------------------
  modAp = ESPUI.addControl(Switcher, "Modo AP", String(memModAP), Wetasphalt, tab6, webModoAP);
  ota = ESPUI.addControl(Switcher, "Activar OTA", String(memModOTA), Wetasphalt, tab6, webOta);
  wifi = ESPUI.addControl(Text, "WiFi", String(miLlave.ssid), Dark, tab6, webssid);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "SSID", None, wifi), labelsPer);
  pass = ESPUI.addControl(Text, "Password", String(miLlave.password), Dark, wifi, webpass);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Contrasena", None, wifi), labelsPer);
  ESPUI.setElementStyle(wifi, inputColor);
  ESPUI.setElementStyle(pass, inputColor);
  ESPUI.setInputType(pass, "password");
  wifiGuard = ESPUI.addControl(Button, "Nuevo WiFi", "Guardar", Alizarin, wifi, webWifiGuard);

  // ESPUI.begin("ESP32UI MAX7219", "admin", hostpass); // Seguridad basica.
  ESPUI.begin("ESP32UI MAX7219");
  delay(100);

  upd_ESPUI();
}
void upd_ESPUI()
{
  ESPUI.updateSelect(horaTipo, String(memRelojUDP));
  ESPUI.updateSelect(climaTipo, String(memClimaOWM));

#ifdef MODO_DHT
  ESPUI.setEnabled(climaTipo, false);
  ESPUI.setPanelStyle(climaTipo, deshabilitar);
#endif

  if (!miAlarma.hor.est)
  {
    ESPUI.setEnabled(alarmSilen, false);
    ESPUI.setPanelStyle(alarmSilen, deshabilitar);
  }

  if (!MOD_RTC)
  {
    ESPUI.setEnabled(horaTipo, false);
    ESPUI.setEnabled(btnRTC, false);
    ESPUI.setPanelStyle(horaTipo, deshabilitar);
    ESPUI.setPanelStyle(btnRTC, deshabilitar);
  }

  if (memAnimAle)
  {
    ESPUI.setEnabled(animEfecto, false);
    ESPUI.setPanelStyle(animEfecto, deshabilitar);
  }
  else
  {
    ESPUI.updateSelect(animEfecto, String(memAnimEfecto));
    ESPUI.setEnabled(animDuracion, false);
    ESPUI.setPanelStyle(animDuracion, deshabilitar);
  }
}

void webVelocCrip(Control *sender, int value)
{
  test(sender, value);
  catCrypto[0].veloc = sender->value.toInt();
  catCrypto[1].veloc = catCrypto[0].veloc;
  setInt("velocCMC", catCrypto[0].veloc);
}
void webVelocFutb(Control *sender, int value)
{
  test(sender, value);
  velocSA = sender->value.toInt();
  setInt("velocSA", velocSA);
}

void webRelojEstado(Control *sender, int value)
{ // Apagado o Encendido
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    memEncendido = true;
    P.displayReset();
    P.displayShutdown(!memEncendido);
    break;
  case S_INACTIVE:
    memEncendido = false;
    P.displayShutdown(!memEncendido);
    P.displayReset();
    break;
  }
}
void webRelojBrillo(Control *sender, int type)
{ // Cambiar brillo
  test(sender, type);
  P.setIntensity(sender->value.toInt());
}
void webClimaTipo(Control *sender, int value)
{ // OpenWheatherMap o DHT22
  test(sender, value);
  memClimaOWM = sender->value.toInt();
  setBool("cliTipo", memClimaOWM);

  if (memClimaOWM)
  {
    leer_OpenWeatherMap();
  }
}
void webRelojTipo(Control *sender, int value)
{ // RTC o NTP
  test(sender, value);
  memRelojUDP = sender->value.toInt();
  setBool("rTipo", memRelojUDP);
}
void webRelojDesf(Control *sender, int value)
{
  test(sender, value);
  memHours = sender->value.toInt();
  delay(10);
  if (syncTime())
  {
    setInt("hours", memHours);
    cabInfo = "Nuevo desface UTC";
    tipMsj = true;
    minAnt = 255;
  }
  else
  {
    cabInfo = "Algo salio mal";
    tipMsj = false;
  }
#if WEB
  webTiEstado(10);
#endif
}
void webRelojVeran(Control *sender, int value)
{
  test(sender, value);
  memDaylight = sender->value.toInt();
  delay(10);
  if (syncTime())
  {
    setInt("daylight", memDaylight);
    cabInfo = "Nuevo horario de verano";
    tipMsj = true;
    minAnt = 255;
  }
  else
  {
    cabInfo = "Algo salio mal";
    tipMsj = false;
  }
#if WEB
  webTiEstado(10);
#endif
}
void webFort12h(Control *sender, int value)
{ // 12H o 24H
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    is12h = true;
    setBool("is12h", is12h);
    break;
  case S_INACTIVE:
    is12h = false;
    setBool("is12h", is12h);
    break;
  }
  minAnt = 255;
}

void webRelojGuardar(Control *sender, int type)
{ // Guardar nueva hora RTC
  test(sender, type);
#if MOD_RTC
  switch (type)
  {
  case B_UP:
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
      cabInfo = "RTC: " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
#if WEB
      webTiEstado(10);
#endif
      tipMsj = true;
    }
    else
    {
      cabInfo = "No se pudo actualizar la hora.";
#if WEB
      webTiEstado(10);
#endif
      tipMsj = false;
    }
    break;
  }
#endif
}
void webRelojDuracion(Control *sender, int value)
{ // Cambiar tiempos de Reloj, Temp y Hum.
  test(sender, value);
  switch (sender->id)
  {
  case 22:
    catVista[0].duracion = sender->value.toInt();
    setInt("tiem0", catVista[0].duracion);
    break;

  case 24:
    catVista[1].duracion = sender->value.toInt();
    setInt("tiem1", catVista[1].duracion);
    break;

  case 26:
    catVista[2].duracion = sender->value.toInt();
    setInt("tiem2", catVista[2].duracion);
    break;

  default:
#if WEB
    cabInfo = "El ID no coincide";
    webTiEstado(10);
#endif
    tipMsj = false;
    break;
  }
}
void webReEst(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    catVista[0].estado = true;
    setBool("horEst", catVista[0].estado);
    break;
  case S_INACTIVE:
    if (catVista[1].estado || catVista[2].estado)
    {
      catVista[0].estado = false;
      setBool("horEst", catVista[0].estado);
    }
    else
    {
      ESPUI.updateSwitcher(horaEst, true);
#if WEB
      cabInfo = "No desactivar todo.";
      webTiEstado(10);
#endif
      tipMsj = false;
    }
    break;
  }
}
void webTeEst(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    catVista[1].estado = true;
    setBool("temEst", catVista[1].estado);
    break;
  case S_INACTIVE:
    if (catVista[0].estado || catVista[2].estado)
    {
      catVista[1].estado = false;
      setBool("temEst", catVista[1].estado);
    }
    else
    {
      ESPUI.updateSwitcher(tempEst, true);
#if WEB
      cabInfo = "No desactivar todo.";
      webTiEstado(10);
#endif
      tipMsj = false;
    }
    break;
  }
}
void webHuEst(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    catVista[2].estado = true;
    setBool("humEst", catVista[2].estado);
    break;
  case S_INACTIVE:
    if (catVista[0].estado || catVista[1].estado)
    {
      catVista[2].estado = false;
      setBool("humEst", catVista[2].estado);
    }
    else
    {
      ESPUI.updateSwitcher(humeEst, true);
#if WEB
      cabInfo = "No desactivar todo.";
      webTiEstado(10);
#endif
      tipMsj = false;
    }
    break;
  }
}
void webCryptoEst1(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    catCrypto[0].estado = true;
    setBool("cry0est", catCrypto[0].estado);
    break;
  case S_INACTIVE:
    if (catVista[0].estado || catVista[1].estado || catVista[2].estado)
    {
      catCrypto[0].estado = false;
      setBool("cry0est", catCrypto[0].estado);
    }
    else
    {
      ESPUI.updateSwitcher(crypto1, true);
#if WEB
      cabInfo = "No desactivar todo.";
      webTiEstado(10);
#endif
      tipMsj = false;
    }
    break;
  }
}
void webCryptoEst2(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    catCrypto[1].estado = true;
    setBool("cry1est", catCrypto[1].estado);
    break;
  case S_INACTIVE:
    if (catVista[0].estado || catVista[1].estado || catVista[2].estado)
    {
      catCrypto[1].estado = false;
      setBool("cry1est", catCrypto[1].estado);
    }
    else
    {
      ESPUI.updateSwitcher(crypto2, true);
#if WEB
      cabInfo = "No desactivar todo.";
      webTiEstado(10);
#endif
      tipMsj = false;
    }
    break;
  }
}
void webDeportEst1(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    catDeport[0].estado = true;
    setBool("dep0est", catDeport[0].estado);
    break;
  case S_INACTIVE:
    if (catVista[0].estado || catVista[1].estado || catVista[2].estado)
    {
      catDeport[0].estado = false;
      setBool("dep0est", catDeport[0].estado);
    }
    else
    {
      ESPUI.updateSwitcher(deport1, true);
#if WEB
      cabInfo = "No desactivar todo.";
      webTiEstado(10);
#endif
      tipMsj = false;
    }
    break;
  }
}
// --------------
void webAnimAleatorio(Control *sender, int value)
{ // Modo aleatorio ON - OFF
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:

    ESPUI.setEnabled(animEfecto, false);
    ESPUI.setPanelStyle(animEfecto, deshabilitar);
    ESPUI.setEnabled(animDuracion, true);
    ESPUI.setPanelStyle(animDuracion, ";");
    memAnimAle = true;
    setBool("anAle", memAnimAle);
    ESPUI.print(cabEstado, info());
    break;

  case S_INACTIVE:
    // String n = "Efecto: " + String(catalogo[memAnimEfecto].nombre);
    ESPUI.updateSelect(animEfecto, String(memAnimEfecto));
    ESPUI.setEnabled(animEfecto, true);
    ESPUI.setPanelStyle(animEfecto, ";");
    ESPUI.setEnabled(animDuracion, false);
    ESPUI.setPanelStyle(animDuracion, deshabilitar);
    memAnimAle = false;
    setBool("anAle", memAnimAle);
    ESPUI.print(cabEstado, info());
    break;
  }
}
void webanimEfecto(Control *sender, int value)
{ // Elegir efecto In y Out
  test(sender, value);
  memAnimEfecto = sender->value.toInt();
  ESPUI.print(cabEstado, info());
  setInt("anEfec", memAnimEfecto);
}
void webAnimDuracion(Control *sender, int value)
{
  test(sender, value);
  memAnimDuracion = sender->value.toInt();
  setInt("anDura", memAnimDuracion);
}
// --------------
void webAlarmHora(Control *sender, int type)
{
  test(sender, type);
  String tmp = sender->value;

  int pos = tmp.indexOf(':');
  if (pos != -1)
  {
    miAlarma.hor.h = tmp.substring(0, pos).toInt();
    miAlarma.hor.m = tmp.substring(pos + 1).toInt();

    if (miAlarma.hor.est)
    {
      ESPUI.print(cabEstado, info());
      setStruct("miAlarm", &miAlarma, sizeof(miAlarma));
    }
  }
  else
  {
#if WEB
    cabInfo = "Formato incorrecto";
    webTiEstado(10);
#endif
    tipMsj = false;
  }
}
void webAlarmRepet(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    miAlarma.repetir = true;
    break;
  case S_INACTIVE:
    miAlarma.repetir = false;
    break;
  }

  if (miAlarma.hor.est)
  {
#if WEB
    ESPUI.print(cabEstado, info());
#endif
    setStruct("miAlarm", &miAlarma, sizeof(miAlarma));
  }
}
void webAlarmSilen(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    test(sender, type);
    miAlarma.silenciar = true;
    break;
  }
}
void webAlarmGuard(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    miAlarma.hor.est = true;
    mensAlarm = true;
    sonando = false;
    setStruct("miAlarm", &miAlarma, sizeof(miAlarma));

#if WEB
    ESPUI.setEnabled(alarmSilen, true);
    ESPUI.setPanelStyle(alarmSilen, ";");
    ESPUI.print(cabEstado, info());
#endif
    break;

  case S_INACTIVE:
    // cancelar alarma
    miAlarma.hor.est = false;
    miAlarma.silenciar = false;
    miAlarma.hor.h = 0;
    miAlarma.hor.m = 0;
    sonando = true;
    setStruct("miAlarm", &miAlarma, sizeof(miAlarma));

#if WEB
    ESPUI.setEnabled(alarmSilen, false);
    ESPUI.setPanelStyle(alarmSilen, deshabilitar);
    ESPUI.print(cabEstado, info());
#endif
    break;
  }
}
// --------------
void webMensTipo(Control *sender, int value)
{
  test(sender, value);
  miMsj.tipo = sender->value.toInt();
  if (miMsj.tipo)
  {
    if (miMsj.pausa == 0)
    {
      ESPUI.updateSlider(mensPausa, 2);
      miMsj.pausa = 2;
    }
  }
  else
  {
    ESPUI.updateSlider(mensPausa, 0);
    miMsj.pausa = 0;
  }
}
void webMensTexto(Control *sender, int value)
{
  test(sender, value);
  sender->value.toCharArray(miMensaje, 50);
}
void webMensIndefi(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    miMsj.indefi = true;
    ESPUI.updateVisibility(mensDura, false);
    break;
  case S_INACTIVE:
    miMsj.indefi = false;
    tAntMensaj = millis();
    ESPUI.updateVisibility(mensDura, true);
    break;
  }
}
void webMensDuracion(Control *sender, int value)
{
  test(sender, value);
  miMsj.duracion = sender->value.toInt();
}
void webMensVelocidad(Control *sender, int value)
{
  test(sender, value);
  miMsj.velocidad = sender->value.toInt();
}
void webMensPausa(Control *sender, int value)
{
  test(sender, value);
  miMsj.pausa = sender->value.toInt();
}
void webMensEnviar(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    test(sender, type);
    if (strlen(miMensaje) > 0)
    {
      miMsj.estado = !miMsj.estado;
      if (miMsj.estado)
      {
        // ESPUI.updateControlValue(mensGuard, String("Cancelar").c_str());
        ESPUI.updateButton(mensGuard, "Cancelar");
        tAntMensaj = millis();
      }
      else
      {
        // ESPUI.updateControlValue(mensGuard, String("Enviar").c_str());
        ESPUI.updateButton(mensGuard, "Enviar");
        ESPUI.updateText(mensText, "");
        // miMensaje = "";
      }
      break;
    }
    else
    {
#if WEB
      cabInfo = "Mensaje vacio.";
      webTiEstado(10);
#endif
      tipMsj = false;
    }
  }
}
void webModoAP(Control *sender, int value)
{ // Modo Estacion: ON - OFF
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    memModAP = true;
    setBool("modoAP", memModAP);
    delay(1000);
    ESP.restart();
    break;

  case S_INACTIVE:
    memModAP = false;
    setBool("modoAP", memModAP);
    delay(1000);
    ESP.restart();
    break;
  }
}
void webOta(Control *sender, int value)
{ // Actualizacion por OTA: ON - OFF
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    setBool("funcOta", true);
    delay(1000);
    ESP.restart();
    break;

  case S_INACTIVE:
    memModAP = false;
    setBool("funcOta", memModAP);
    break;
  }
}
void webssid(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;
  tmp.toCharArray(miLlave.ssid, sizeof(miLlave.ssid));
}
void webpass(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;
  tmp.toCharArray(miLlave.password, sizeof(miLlave.password));
}
void webWifiGuard(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    test(sender, type);
    if (strlen(miLlave.ssid) > 3 && strlen(miLlave.password) > 7)
    {
      setStruct("miCreden", &miLlave, sizeof(miLlave));
      setBool("modoAP", false);
      delay(100);
      if (WiFi.getMode() == WIFI_AP)
      {
        ESP.restart();
      }
    }
    else
    {
#if WEB
      cabInfo = "ssid o password no valido";
      webTiEstado(10);
#endif
      tipMsj = false;
    }
  }
}
void webApiOWM(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 20)
  {
    return;
  }
  tmp.toCharArray(miLlave.keyWeather, sizeof(miLlave.keyWeather));
}
void webOWMCode(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 8)
  {
    return;
  }
  tmp.replace(", ", ",");
  tmp.toCharArray(miLlave.city, sizeof(miLlave.city));
}

void webApiCMC(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 20)
  {
    return;
  }
  tmp.toCharArray(miLlave.keyCripto, sizeof(miLlave.keyCripto));
}
void webCMCCripto1(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 2)
  {
    return;
  }
  tmp.toCharArray(miLlave.criptos[0], sizeof(miLlave.criptos[0]));
}
void webCMCCripto2(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 2)
  {
    return;
  }
  tmp.toCharArray(miLlave.criptos[1], sizeof(miLlave.criptos[1]));
}
void webCMCDivisa(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 2)
  {
    return;
  }
  tmp.toCharArray(miLlave.divisa, sizeof(miLlave.divisa));
}

void webApiSA1(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 20)
  {
    return;
  }
  tmp.toCharArray(miLlave.api_key[0], sizeof(miLlave.api_key[0]));
}
void webApiSA2(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 20)
  {
    return;
  }
  tmp.toCharArray(miLlave.api_key[1], sizeof(miLlave.api_key[1]));
}
void webSATeam(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 3)
  {
    return;
  }
  tmp.toCharArray(miLlave.equipo, sizeof(miLlave.equipo));
}
void webSALocation(Control *sender, int value)
{
  test(sender, value);
  String tmp = sender->value;

  if (tmp.length() < 8)
  {
    return;
  }
  tmp.replace(", ", ",");
  tmp.toCharArray(miLlave.location, sizeof(miLlave.location));
}

void webApiGuard(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    test(sender, type);
    String x = sender->label;
    if (x == "Nuevos SA")
    {
      ESPUI.updateText(key1SA, "");
      ESPUI.updateText(key2SA, "");
      flagSA = true;
    }
    else if (x == "Nuevos CMC")
    {
      tAntCriptUpd = 0;
      ESPUI.updateText(apiCMC, "");
    }
    else if (x == "Nuevos OWM")
    {
      ESPUI.updateText(apiOWM, "");
    }

    setStruct("miCreden", &miLlave, sizeof(miLlave));
#if WEB
    cabInfo = "Configuracion API guardado";
    webTiEstado(10);
#endif
    tipMsj = true;
    break;
  }
}
void espReiniciar(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    ESP.restart();
    break;
  }
}
// --------------
void webFont(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    P.setFont(ASCII_Dobl);
    setBool("tipFont", true);
    ESP.restart();
    break;
  case S_INACTIVE:
    P.setFont(ASCII_Simp);
    setBool("tipFont", false);
    ESP.restart();
    break;
  }
}
void test(Control *sender, int type)
{
  Serial.println(F("------------------"));
  Serial.print(F("ID:"));
  Serial.print(sender->id);
  Serial.print(F(" - Type:"));
  Serial.print(type);
  Serial.print(F(" - '"));
  Serial.print(sender->label);
  Serial.print(F("' = "));
  Serial.println(sender->value);
}
#endif

// ----------------------------------------
void getDatos()
{
  mem.begin("web", false);
  delay(20);

  catVista[0].estado = mem.getBool("horEst", true);
  catVista[1].estado = mem.getBool("temEst", true);
  catVista[2].estado = mem.getBool("humEst", true);
  catCrypto[0].estado = mem.getBool("cry0est", true);
  catCrypto[1].estado = mem.getBool("cry1est", true);
  catDeport[0].estado = mem.getBool("dep0est", true);

  catVista[0].duracion = mem.getInt("tiem0", 10);
  catVista[1].duracion = mem.getInt("tiem1", 10);
  catVista[2].duracion = mem.getInt("tiem2", 10);
  catCrypto[0].veloc = mem.getInt("velocCMC", 30);
  catCrypto[1].veloc = catCrypto[0].veloc;
  velocSA = mem.getInt("velocSA", 30);
  countSA = mem.getInt("counSA", 0);

  memRelojUDP = mem.getBool("rTipo", true);
  memHours = mem.getInt("hours", -4);
  memDaylight = mem.getInt("daylight", 0);
  is12h = mem.getBool("is12h", false);

  memClimaOWM = mem.getBool("cliTipo", true);
  memAnimAle = mem.getBool("anAle", true);
  memAnimEfecto = mem.getInt("anEfec", 1);
  memAnimDuracion = (byte)mem.getInt("anDura", 10);

  memModAP = mem.getBool("modoAP", false);
  memModOTA = mem.getBool("funcOta", false);
  fontSelect = mem.getBool("tipFont", false);

  mem.getBytes("miCreden", &miLlave, sizeof(miLlave));
  mem.getBytes("miAlarm", &miAlarma, sizeof(miAlarma));

  mem.end();
}
void setBool(const char *d, bool v)
{
  mem.begin("web", false);
  mem.putBool(d, v);
  delay(1);
  mem.end();
}
void setInt(const char *d, int v)
{
  mem.begin("web", false);
  mem.putInt(d, v);
  mem.end();
}
void setStruct(const char *etiq, void *estruct, size_t tam)
{
  if (mem.begin("web", false))
  {
    size_t escritos = mem.putBytes(etiq, estruct, tam);
    mem.end();

    if (escritos == tam)
    {
      Serial.printf("NVS: '%s' guardado correctamente (%d bytes)\n", etiq, tam);
    }
    else
    {
      Serial.printf("NVS: Error al guardar '%s'\n", etiq);
    }
  }
}

uint8_t animProgress(uint8_t barr)
{
  MD_MAX72XX *mx = P.getGraphicObject();
  uint8_t maxX = (MAX_DEVICES * 8) - 1;
  uint8_t n = maxX - barr;
  for (int x = maxX; x > n; x--)
  {
    mx->setPoint(7, x, true);
  }

  for (int i = 0; i <= n; i++)
  {
    if (i != 0)
    {
      mx->setPoint(7, i - 1, false);
    }
    mx->setPoint(7, i, true);
    delay(10);
  }
  barr++;
  return barr;
}
void mostEst(String x, uint8_t n)
{
  bool var = true;
  for (byte i = 0; i < n; i++)
  {
    if (var)
    {
      P.print(x);
      delay(350);
    }
    else
    {
      P.print(" ");
      delay(300);
    }
    var = !var;
  }
}

void conect_WiFi()
{
  delay(50);
  if (!memModAP) // MODO STA
  {
    uint8_t timeout = 60;
    uint8_t barra = 0;

    WiFi.setHostname(hostname);
    WiFi.mode(WIFI_STA);
    Serial.print(F("Modo Station:"));
    WiFi.begin(miLlave.ssid, miLlave.password);

    while (WiFi.status() != WL_CONNECTED && timeout > 0) // MODO CLIENTE
    {
      Serial.print(F("."));
      timeout--;

      P.setTextAlignment(PA_LEFT);
      if (barra == MAX_DEVICES * 8)
      {
        P.displayClear();
        barra = 0;
      }
      barra = animProgress(barra);
    }

    if (timeout > 0)
    {
      if (stable_Connect())
      {
        syncTime();
      }
      else
      {
#if MOD_RTC
        memRelojUDP = false;
#endif
      }
    }
    else
    {
      modoAP();
    }
    WiFi.setSleep(false);
  }
  else // MODO AP
  {
    modoAP();
  }

  if (!MDNS.begin(mdns))
  {
    Serial.println("Error: MDNS");
    mostEst("xmDNS", 5);
  }

  P.setTextAlignment(PA_CENTER);
  String mode = (WiFi.getMode() == WIFI_AP) ? "<AP>" : "<STA>";
  mostEst(mode, 5);

  miIP = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
  Serial.println("\n" + mode + ": " + miIP.toString());
}
void modoAP()
{
  Serial.print(F("Modo Access Point:"));
  memRelojUDP = false;
  memClimaOWM = false;

  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAPConfig(miIP, miIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(hostname, hostpass);

  int connect_timeout = 10;
  do
  {
    delay(400);
    Serial.print(".");
    connect_timeout--;
  } while (connect_timeout);
  WiFi.setSleep(false);
}
bool stable_Connect()
{
  delay(1000);
  WiFiClient client;
  const char *host = "www.google.com";
  const uint16_t port = 80;

  if (client.connect(host, port))
  {
    client.stop();
    return true;
  }
  client.stop();
  return false;
}
bool syncTime()
{
  Serial.print("\nSincronizando hora");

  int intentos = 0;
  configTime((memHours * 3600), memDaylight * 3600, ntpServer);
  time_t now = time(nullptr);

  while (now < 24 * 3600 && intentos < 100)
  {
    delay(100);
    now = time(nullptr);
    intentos++;
    Serial.print(".");

    if (intentos >= 100)
    {
      Serial.println("\nFallo en la sincronización.");
      return false;
    }
  }
  Serial.println("\n");
  return true;
}

#if OTA
void funcOta()
{
  setBool("funcOta", false);
  InitOTA();

  bool var = true;
  while (true)
  {
    ArduinoOTA.handle();
    if (var)
    {
      P.print("uOTA");
    }
    else
    {
      P.print(" ");
    }
    var = !var;
    delay(200);
  }
}
#endif

#if WEB
void webTiEstado(uint8_t x)
{
  tMsjWeb = x;
  tAntWebCab = millis();
}
void webSiEstado()
{
  if (millis() - tAntWebCab < tMsjWeb * 1000)
  {
    if (tipMsj)
    {
      ESPUI.getControl(cabEstado)->color = ControlColor::Peterriver;
    }
    else
    {
      ESPUI.getControl(cabEstado)->color = ControlColor::Alizarin;
    }
    ESPUI.updateControl(cabEstado);
    ESPUI.print(cabEstado, info());
  }
  else
  {
    ESPUI.getControl(cabEstado)->color = ControlColor::Turquoise;
    ESPUI.updateControl(cabEstado);
    cabInfo = "";
    ESPUI.print(cabEstado, info());
  }
}
void WebMostrarMsj()
{
  if (miMsj.indefi)
  {
    P.displayText(miMensaje, PA_CENTER, miMsj.velocidad, miMsj.pausa * 1000, catalogo[3].efecto, catalogo[3].efecto);
  }
  else
  {

    if ((millis() - tAntMensaj) < (miMsj.duracion * 60000))
    {
      if (miMsj.tipo)
      {
        P.displayText(miMensaje, PA_CENTER, miMsj.velocidad, miMsj.pausa * 1000, catalogo[2].efecto, catalogo[2].efecto);
      }
      else
      {
        P.displayText(miMensaje, PA_CENTER, miMsj.velocidad, miMsj.pausa * 1000, catalogo[3].efecto, catalogo[3].efecto);
      }
    }
    else
    {
      // miMensaje = "";
      miMsj.estado = false;
      // ESPUI.updateControlValue(mensGuard, String("Enviar").c_str());
      ESPUI.updateButton(mensGuard, "Enviar");
      ESPUI.print(mensText, "");
    }
  }
}
void sonarAlarma()
{
  if (miAlarma.hor.h == miTiempo.hora && miAlarma.hor.m == miTiempo.minuto)
  { // Alarma sonando
    sonando = true;

    if (mensAlarm)
    {
      P.displayReset();
      // P.displayText(" ", PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
      P.print("");
      mensAlarm = false;
    }

    if (P.displayAnimate())
    {
      P.displayText("<< Alarma >>", PA_CENTER, (miMsj.velocidad), 0, catalogo[3].efecto, catalogo[3].efecto);
    }

#if ENCODER
    miAlarma.silenciar = (digitalRead(BTN_SW) == LOW) ? true : miAlarma.silenciar; // Silenciar
    delay(100);
#endif
    if (miAlarma.silenciar)
    {
      digitalWrite(buzzer, LOW);
    }
    else
    {
      digitalWrite(buzzer, HIGH);
      Serial.println("Sonando...");
    }
  }
  else if (sonando)
  { // Alarma despues de sonar o desactivado
    sonando = false;
    miAlarma.silenciar = false;
    mensAlarm = true;
    digitalWrite(buzzer, LOW);

    if (!miAlarma.repetir)
    {
      miAlarma.hor.h = 0;
      miAlarma.hor.m = 0;
      miAlarma.hor.est = false;
      setStruct("miAlarm", &miAlarma, sizeof(miAlarma));
      Serial.println("luego de sonar");

#if WEB
      ESPUI.updateSwitcher(alarmRep, miAlarma.repetir);
      ESPUI.setEnabled(alarmSilen, false);
      ESPUI.setPanelStyle(alarmSilen, deshabilitar);
      ESPUI.print(cabEstado, info());
#endif
    }
  }
}
#endif
String info()
{
  String x = memAnimAle ? "Efecto: Aleatorio" : "Efecto: " + String(catalogo[memAnimEfecto].nombre);

  if (miAlarma.hor.est)
  {
    x += "<br>Alarma: " + strHora(miAlarma.hor.h, miAlarma.hor.m) + (miAlarma.repetir ? " Repetir" : "");
  }
  else
  {
    x += "<br>Alarma: Ninguno";
  }

  if (cabInfo.length() > 0)
  {
    x += "<br>" + cabInfo;
  }
  return x;
}

void leerDHT()
{
  float t, h;
#if MOD_DHT
  t = dht.readTemperature() + (CALIB_TEMP);
  h = dht.readHumidity();
#endif

  if (isnan(t) || isnan(h))
  {
    t = 00.0;
    h = 00.0;
    strcpy(catVista[1].datos, "xDHT");
    strcpy(catVista[2].datos, "xDHT");
#if WEB
    cabInfo = "Error de lectura DHT";
    webTiEstado(10);
#endif
    tipMsj = false;
  }
  snprintf(catVista[1].datos, sizeof(catVista[1].datos), "%.1f&", t);
  snprintf(catVista[2].datos, sizeof(catVista[2].datos), "%.1f%s", h, "%");
}

void leer_Hora(bool &udp)
{
  if (udp)
  {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
      miTiempo.hora = timeinfo.tm_hour;
      miTiempo.minuto = timeinfo.tm_min;
      miTiempo.dia = timeinfo.tm_mday;
      miTiempo.mes = timeinfo.tm_mon + 1;
      miTiempo.estado = true;
      return;
    }
    miTiempo.estado = false;
    return;
  }
  else
  {
#if MOD_RTC
    if (!rtc.lostPower())
    {
      DateTime tiempo = rtc.now();
      miTiempo.hora = tiempo.hour();
      miTiempo.minuto = tiempo.minute();
      miTiempo.dia = tiempo.day();
      miTiempo.mes = tiempo.month();
      miTiempo.estado = true;
      return;
    }
    miTiempo.estado = false;
    return;
#endif
  }
}
void modoHora()
{
  leer_Hora(memRelojUDP);
  if (!miTiempo.estado)
  {
    const char *etiqueta = memRelojUDP ? "UDP" : "RTC";

    if (minAnt != 255)
    {
      snprintf(catVista[0].datos, sizeof(catVista[0].datos), "x%s", etiqueta);
      minAnt = 255; // Flag

#if WEB
      cabInfo = "Error " + String(etiqueta);
      webTiEstado(10);
#endif
      tipMsj = false;

#if WEB
      ESPUI.print(cabEstado, cabInfo);
#endif
    }
    return;
  }

  if (minAnt != miTiempo.minuto)
  {
    if (is12h)
    {
      String periodo = (miTiempo.hora >= 12) ? "PM" : "AM";
      int hora12 = miTiempo.hora % 12;
      if (hora12 == 0)
        hora12 = 12;

      snprintf(catVista[0].datos, sizeof(catVista[0].datos), "%02d:%02d %s", hora12, miTiempo.minuto, periodo);
    }
    else
    {
      snprintf(catVista[0].datos, sizeof(catVista[0].datos), "%02d:%02d", miTiempo.hora, miTiempo.minuto);
    }
    minAnt = miTiempo.minuto;
  }
}

void leer_OpenWeatherMap()
{
  if (WiFi.status() != WL_CONNECTED || memModAP)
  {
    strcpy(catVista[1].datos, "xWiFi");
    strcpy(catVista[2].datos, "xWiFi");
    Serial.println("OWM -> Sin conexión WiFi");
    return;
  }

  float t = NAN, h = NAN;
  WiFiClient client;
  HTTPClient http;

  String city = String(miLlave.city);
  city.replace(" ", "%20");

  String apiUrl = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "&units=metric&APPID=" + String(miLlave.keyWeather);
  http.begin(client, apiUrl);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    t = h = 0.0;
    strcpy(catVista[1].datos, "xOWM");
    strcpy(catVista[2].datos, "xOWM");

#if WEB
    cabInfo = "Error de openWeatherMap";
    webTiEstado(10);
#endif
    tipMsj = false;
    http.end();
    // Serial.println("OWM -> httpCode: " + String(httpCode));
    return;
  }

  String payload = http.getString();
  // JSONVar data = JSON.parse(payload);
  DynamicJsonDocument data(1024);
  DeserializationError err = deserializeJson(data, payload);
  // Serial.printf("OWM - Memoria JSON: %d bytes\n", data.size());

  if (!data.isNull())
  {
    t = (double)data["main"]["temp"];
    h = (double)data["main"]["humidity"];
  }

  if (isnan(t) || isnan(h) || err)
  {
    t = h = 0.0;
    strcpy(catVista[1].datos, "xJSON");
    strcpy(catVista[2].datos, "xJSON");
    http.end();

    Serial.println(F("OWM -> Error al parsear JSON"));
    return;
  }

  snprintf(catVista[1].datos, sizeof(catVista[1].datos), "%.1f&", t);
  snprintf(catVista[2].datos, sizeof(catVista[2].datos), "%.1f%s", h, "%");
  http.end();
}
void modoClima()
{
  if (memClimaOWM)
  {
    unsigned long tActual = millis();
    if (tActual - tAntClimaUpd >= UPDTIME_OWM)
    {
      leer_OpenWeatherMap();
      tAntClimaUpd = tActual;
    }
  }
  else
  {
    leerDHT();
  }
}

void leer_CoinMarketCap()
{
  if (WiFi.status() != WL_CONNECTED || memModAP)
  {
    strcpy(catCrypto[0].datos, "xWiFi");
    strcpy(catCrypto[1].datos, "xWiFi");
    Serial.println(F("CMC -> Sin conexión WiFi"));
    return;
  }

  HTTPClient http;
  String apiUrl = "https://pro-api.coinmarketcap.com/v1/cryptocurrency/quotes/latest?symbol=" + String(miLlave.criptos[0]) + "," + String(miLlave.criptos[1]) + "&convert=" + String(miLlave.divisa);

  http.begin(apiUrl);
  http.addHeader("Accepts", "application/json");
  http.addHeader("X-CMC_PRO_API_KEY", String(miLlave.keyCripto));

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    strcpy(catCrypto[0].datos, "xCMC");
    strcpy(catCrypto[1].datos, "xCMC");

#if WEB
    cabInfo = "Error de coinMarketCap";
    webTiEstado(10);
#endif
    tipMsj = false;
    http.end();
    return;
  }

  String payload = http.getString();
  // JSONVar data = JSON.parse(payload);
  DynamicJsonDocument data(4096);
  DeserializationError err = deserializeJson(data, payload);
  // Serial.printf("CMC - Memoria JSON: %d bytes\n", data.size());

  // if (JSON.typeof(data) == "undefined")
  if (data.isNull() || err)
  {
    strcpy(catCrypto[0].datos, "xJSON");
    strcpy(catCrypto[1].datos, "xJSON");
    cabInfo = "Error de coinMarketCap";
#if WEB
    webTiEstado(10);
#endif
    tipMsj = false;
    http.end();

    Serial.println(F("CMC -> Error al parsear JSON"));
    return;
  }
  float crypto1, crypto2;
  crypto1 = double(data["data"][String(miLlave.criptos[0])]["quote"][String(miLlave.divisa)]["price"]);
  crypto2 = double(data["data"][String(miLlave.criptos[1])]["quote"][String(miLlave.divisa)]["price"]);

  long entero = (long)crypto1;
  snprintf(catCrypto[0].datos, sizeof(catCrypto[0].datos), "%s %ld.%03ld %s", miLlave.criptos[0], entero / 1000, entero % 1000, miLlave.divisa);
  entero = (long)crypto2;
  snprintf(catCrypto[1].datos, sizeof(catCrypto[1].datos), "%s %ld.%03ld %s", miLlave.criptos[1], entero / 1000, entero % 1000, miLlave.divisa);

  // Serial.printf("(%s): $%.2f USD\n", miLlave.criptos[0], crypto1);
  // Serial.printf("(%s): $%.2f USD\n", miLlave.criptos[1], crypto2);

  http.end();
}
void modoCrypto()
{
  unsigned long tActual = millis();
  if (tActual - tAntCriptUpd > UPDTIME_CMC) // 5 min.
  {
    leer_CoinMarketCap();
    tAntCriptUpd = tActual;
  }
}

void leerCreditoSerpApi()
{
  delay(200);
  char url[120];
  int result = snprintf(url, sizeof(url), "https://serpapi.com/account.json?api_key=%s", miLlave.api_key[countSA]);

  HTTPClient http;
  http.begin(url);
  if (http.GET() != HTTP_CODE_OK)
  {
    Serial.println("Error http");
  }

  DynamicJsonDocument doc(1600);
  DeserializationError err = deserializeJson(doc, http.getStream());
  if (err)
  {
    Serial.println("Error parse account JSON");
    http.end();
    return;
  }

  const char *statusSA = doc["account_status"] | ""; // Estado
  restSA = doc["plan_searches_left"] | 250;          // Restante mensual

  if (restSA == 1)
  {
    countSA = (countSA + 1) % 2;
    setInt("counSA", countSA);
  }

  Serial.println("--- Cuenta SerpApi ---");
  Serial.printf("Estado: %s - Restante mensual: %d\n", statusSA, restSA);
  http.end();
}
void getResultFutbol(JsonObject obj, bool esSpotlight)
{
  // Limpiar toda la estructura antes de llenarla (opcional)
  memset(&fxInfo, 0, sizeof(futbol));
  strncpy(fxInfo.torneo, obj["tournament"] | obj["league"] | "Unknown", sizeof(fxInfo.torneo));

  strncpy(fxInfo.local, obj["teams"][0]["name"] | "N/A", sizeof(fxInfo.local));
  for (int i = 0; fxInfo.local[i]; i++)
  {
    fxInfo.local[i] = toupper((unsigned char)fxInfo.local[i]);
  }

  strncpy(fxInfo.visita, obj["teams"][1]["name"] | "N/A", sizeof(fxInfo.visita));
  for (int i = 0; fxInfo.visita[i]; i++)
  {
    fxInfo.visita[i] = toupper((unsigned char)fxInfo.visita[i]);
  }

  strncpy(fxInfo.sLocal, obj["teams"][0]["score"] | "", sizeof(fxInfo.sLocal));
  strncpy(fxInfo.sVisita, obj["teams"][1]["score"] | "", sizeof(fxInfo.sVisita));
  fxInfo.min = obj["in_game_time"]["minute"] | 0;
  fxInfo.minExt = obj["in_game_time"]["stoppage"] | 0;
  fxInfo.estado = obj.containsKey("status") ? estadoAEnum(obj["status"]) : FX;

  // Lógica de Fecha y Tiempo
  const char *dateRaw = obj["date"] | "";
  const char *timeRaw = obj["time"] | "";

  if (esSpotlight)
  {
    // Formato esperado: "tomorrow, 4:30 PM"
    const char *coma = strchr(dateRaw, ',');
    if (coma != NULL)
    {
      int lenDia = coma - dateRaw;
      if (lenDia > (int)sizeof(fxInfo.dia) - 1)
        lenDia = sizeof(fxInfo.dia) - 1;
      strncpy(fxInfo.dia, dateRaw, lenDia);
      parsDateSA();

      parsTimeSA(coma + 2); // Procesar "4:30 PM"
    }
  }
  else
  {
    // Formato esperado: "Wed, Jan 21" (date) y "4:00 PM" (time)
    const char *coma = strchr(dateRaw, ',');
    if (coma != NULL)
    {
      int lenDia = coma - dateRaw;
      if (lenDia > (int)sizeof(fxInfo.dia) - 1)
        lenDia = sizeof(fxInfo.dia) - 1;
      strncpy(fxInfo.dia, dateRaw, lenDia);
      parsDateSA();
    }
    else
    {
      strncpy(fxInfo.dia, dateRaw, sizeof(fxInfo.dia) - 1);
      parsDateSA();
    }
    parsTimeSA(timeRaw); // Procesar "4:30 PM"
  }
  imprimir();
}
void leer_serpApi(bool noCache)
{
  if (WiFi.status() != WL_CONNECTED || memModAP)
  {
    snprintf(fxInfo.torneo, sizeof(fxInfo.torneo), "%s", "xWiFi");
    fxInfo.estado = error;
    return;
  }

  char url[250];
  int result = snprintf(url, sizeof(url), "https://serpapi.com/search.json?q=%s&location=%s&hl=en&api_key=%s&json_restrictor=sports_results&no_cache=%s",
                        miLlave.equipo, miLlave.location, miLlave.api_key[countSA], noCache ? "true" : "false");

  if (result >= sizeof(url))
  {
    snprintf(fxInfo.torneo, sizeof(fxInfo.torneo), "%s", "xURL");
    fxInfo.estado = error;
    return;
  }

  int count = 0;
  int code = -1;
  limpiarURL(url);
  // Serial.println(String(url));

  HTTPClient http;
  do
  {
    if (http.begin(url))
    {
      code = http.GET();
      if (code == HTTP_CODE_OK)
      {
        break;
      }
      http.end();
    }
    count++;
    delay(100);

  } while (code != HTTP_CODE_OK && count < 2);

  if (code != HTTP_CODE_OK)
  {
    snprintf(fxInfo.torneo, sizeof(fxInfo.torneo), "http %d", code);
    fxInfo.estado = error;
    http.end();
    return;
  }

  restSA--;
  if (restSA <= 1)
  {
    leerCreditoSerpApi();
  }

  // Usar el Stream directamente para ahorrar un buffer de String intermedio.
  DynamicJsonDocument doc(4500); // bytes
  DeserializationError err = deserializeJson(doc, http.getStream());
  // Serial.printf("SA - Memoria JSON: %d bytes\n", doc.size());

  if (err)
  {
    snprintf(fxInfo.torneo, sizeof(fxInfo.torneo), "%s", "xJSON");
    fxInfo.estado = error;
    http.end();
    doc.clear();
    return;
  }

  const char *er = doc["error"] | "";
  if (strlen(er) > 0)
  {
    snprintf(fxInfo.torneo, sizeof(fxInfo.torneo), "%s", "Cuenta SerpApi");
    fxInfo.estado = error;
    http.end();
    doc.clear();
    return;
  }

  JsonObject sports_results = doc["sports_results"];
  bool mostrado = false;

  if (sports_results.containsKey("game_spotlight"))
  {
    JsonObject spotlight = sports_results["game_spotlight"];
    const char *fecha = spotlight["date"] | "";

    if (strcasestr(fecha, "today") || strcasestr(fecha, "tomorrow"))
    {
      getResultFutbol(spotlight, true);
      mostrado = true;
    }
  }

  if (!mostrado && sports_results.containsKey("games"))
  {
    JsonArray games = sports_results["games"];
    for (JsonObject partido : games)
    {
      if (!partido.containsKey("status"))
      {
        getResultFutbol(partido, false);
        mostrado = true;
        break;
      }
    }
  }
  else
  {
    fxInfo.estado = vacio;
  }

  doc.clear();
  http.end();
}

String mostrarFutbol()
{
  char txt[130];
  const char *textoEst = estadoATexto(fxInfo.estado);
  switch (fxInfo.estado)
  {
  case FX:
    snprintf(txt, sizeof(txt), "%s: %02d:%02d, %s vs %s, %s", fxInfo.dia, fxInfo.fechaHora, fxInfo.fechaMinuto, fxInfo.local, fxInfo.visita, fxInfo.torneo);
    break;
  case LIVE:
    if (fxInfo.minExt > 0)
    {
      snprintf(txt, sizeof(txt), "%s: %d+%d min, %s %s - %s %s", textoEst, fxInfo.min, fxInfo.minExt, fxInfo.local, fxInfo.sLocal, fxInfo.sVisita, fxInfo.visita);
    }
    else
    {
      snprintf(txt, sizeof(txt), "%s: +%d min, %s %s - %s %s", textoEst, fxInfo.min, fxInfo.local, fxInfo.sLocal, fxInfo.sVisita, fxInfo.visita);
    }
    break;
  case HT:
    snprintf(txt, sizeof(txt), "%s: %s %s - %s %s", textoEst, fxInfo.local, fxInfo.sLocal, fxInfo.sVisita, fxInfo.visita);
    break;
  case FT:
    snprintf(txt, sizeof(txt), "%s: %s %s - %s %s", textoEst, fxInfo.local, fxInfo.sLocal, fxInfo.sVisita, fxInfo.visita);
    break;
  case vacio:
    snprintf(txt, sizeof(txt), "%s", "Sin Partidos");
    break;
  default:
    snprintf(txt, sizeof(txt), "%s: %s", textoEst, fxInfo.torneo);
    break;
  }
  return String(txt);
}
void modoFutbol()
{
  if (flagSA)
  {
    leer_serpApi(true);
    flagSA = false;
    return;
  }

  static bool current = false;
  static unsigned long tAntCurrent = 0;

  if (current || fxInfo.estado == LIVE)
  {
    if (millis() - tAntCurrent > UPDTIME_SA)
    {
      Serial.println(F("Actualizando datos del partido..."));
      leer_serpApi(true);

      if (fxInfo.estado == FT)
      {
        Serial.println(F("Partido terminado."));
        current = false;
      }
      tAntCurrent = millis();
    }
    return;
  }

  if (fxInfo.estado == FX && miTiempo.hora == fxInfo.fechaHora &&
      miTiempo.dia == fxInfo.fechaDia && miTiempo.mes == fxInfo.fechaMes)
  {
    if (miTiempo.minuto > fxInfo.fechaMinuto && miTiempo.minuto < fxInfo.fechaMinuto + 3)
    {
      Serial.println(F("Iniciando partido."));
      leer_serpApi(true);
      current = true;
      tAntCurrent = millis();
    }
  }
}

void updateEfecto()
{
  if (memAnimAle)
  {
    if ((millis() - tAntEfecto) > (memAnimDuracion * 60000))
    {
      // miEfecto = random(2, (sizeof(catalogo)/sizeof(catalogo[0])));
      miEfecto += miEfecto;
      miEfecto = miEfecto > 14 ? 2 : miEfecto;
      tAntEfecto = millis();
    }
  }
  else
  {
    miEfecto = memAnimEfecto;
  }
}
int updatePantalla()
{
  const uint8_t m1 = sizeof(catVista) / sizeof(catVista[0]);
  const uint8_t m2 = sizeof(catCrypto) / sizeof(catCrypto[0]);
  const uint8_t m3 = sizeof(catDeport) / sizeof(catDeport[0]);
  const uint8_t TOTALCARRUSEL = m1 + m2 + m3;

  if (runReloj || modo == vTimer)
  {
    return modo;
  }

  bool enable;
  static int i = modo;

  do
  {
    i = (i + 1) % TOTALCARRUSEL;
    if (i < m1)
    {
      enable = catVista[i].estado;
    }
    else if (i < (m1 + m2))
    {
      enable = catCrypto[i - m1].estado;
    }
    else
    {
      enable = catDeport[i - m1 - m2].estado;
    }

    if (i == vReloj)
    {
      updateEfecto();
      posiReloj = 0;
      runReloj = true;
      tAntSegdr = millis();
    }
  } while (!enable);

  static long tAntAP = millis();
  if ((millis() - tAntAP) > 900000)
  {
    if ((WiFi.getMode() == WIFI_AP) && !memModAP)
    {
      ESP.restart();
    }
    tAntAP = millis();
  }

  modo = (vistas)i;
  return modo;
}

void RljSegundero()
{
  if ((millis() - tAntSegdr) > (catVista[modo].duracion) * 1000)
  {
    posiReloj = 2;
  }
}

#if MOD_MICRO
void microfono()
{
  static byte flanco = 0;
  static unsigned long tAntAplau = millis();
  static unsigned long tAntSon = millis();
  if (tAntAplau < millis())
  {
    if (digitalRead(micro) == HIGH)
    {
      flanco = flanco + 1;

      if (tAntSon < millis())
      {
        flanco = 0;
        tAntSon = millis() + 1500;
      }

      if (flanco == 4)
      {
        // P.displayClear();
        P.displayReset();
        P.print("");
        tAntAplau = millis() + 1600;
        flanco = 0;
        tAntSegdr = 0;
      }
    }
  }
}
#endif

#if ENCODER
bool btnEncoder()
{
  static bool btnPresionado = false;
  static unsigned long tInicio = 0;
  static bool posibleLarga = false;

  bool estado = digitalRead(BTN_SW);
  if (!estado && !btnPresionado)
  {
    btnPresionado = true;
    posibleLarga = false;
    tInicio = millis();
  }

  if (!estado && btnPresionado && (millis() - tInicio >= 1300))
  {
    posibleLarga = true;
  }

  if (estado && btnPresionado)
  {
    btnPresionado = false;
    if (posibleLarga)
    {
      delay(50);
      posibleLarga = false;
      return true;
    }
  }
  return false;
}
int selectTimer(int est)
{
  static uint16_t valor = 0;
  if (!digitalRead(BTN_SW))
  {
    delay(400);
    est += 1;

    if (est == 2)
    {
      duracion = (miTimer.m * 60UL + miTimer.s) * 1000UL;
      tiempoInicio = millis();
    }
    else if (est == 3)
    {
      miTimer.m = 0;
      miTimer.s = 0;
      valor = 0;
    }
  }
  return est;
}
String obtenerTimer(int est)
{
  char buffer[7];
  bool sMin = (est == 99);
  int valor = sMin ? miTimer.m : miTimer.s;

  currCLKState = digitalRead(A_CLK);
  if (currCLKState != lastCLKState)
  {
    if (digitalRead(B_DT) != currCLKState)
      valor++;
    else
      valor--;

    if (valor > est)
      valor = 0;
    else if (valor < 0)
      valor = est;
  }

  if (sMin)
    miTimer.m = valor;
  else
    miTimer.s = valor;

  if (rSeg)
    sprintf(buffer, sMin ? "--:%02u" : "%02u:--", sMin ? miTimer.s : miTimer.m);
  else
    sprintf(buffer, "%02u:%02u", miTimer.m, miTimer.s);

  static unsigned long tAnt = 0;
  if ((millis() - tAnt) > 400)
  {
    rSeg = !rSeg;
    tAnt = millis();
  }

  lastCLKState = currCLKState;
  return String(buffer);
}
String contaTimer()
{
  static bool parp = true;
  long restante_ms = duracion - (millis() - tiempoInicio);
  if (restante_ms < 0)
    restante_ms = 0;

  unsigned int totalSegundos = restante_ms / 1000;
  unsigned int mm = totalSegundos / 60;
  unsigned int ss = totalSegundos % 60;

  char buffer[6];
  if (mm == 0 && ss == 0)
  {
    if (parp)
    {
      sprintf(buffer, "00:00");
    }
    else
    {
      sprintf(buffer, " ");
    }
    digitalWrite(buzzer, HIGH);

    delay(100);
    parp = !parp;
    return String(buffer);
  }

  sprintf(buffer, "%02u:%02u", mm, ss);
  return String(buffer);
}
#endif

// ------------------------------------
void setup()
{
  Wire.begin();
  Serial.begin(115200);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print(F("Motivo de reinicio: "));
  Serial.println(resetReason(reason));
  if (!SPIFFS.begin(true))
  {
    Serial.println("Error montando SPIFFS");
  }
  imprimirSPIFFS();

  Serial.println(F("\n\n=============="));
#if MOD_MICRO
  pinMode(micro, INPUT);
#endif
  pinMode(buzzer, OUTPUT);
  randomSeed(analogRead(A0));
  getDatos();

#if MOD_DHT
  dht.begin();
#else
  memClimaOWM = true;
#endif

#if ENCODER
  pinMode(A_CLK, INPUT);
  pinMode(B_DT, INPUT);
  pinMode(BTN_SW, INPUT);
  lastCLKState = digitalRead(A_CLK);
#endif

#if MOD_RTC
  if (!rtc.begin())
    Serial.println(F("No se encontro el RTC"));
#endif

  // MAX7219
  delay(2500);
  P.begin();
  P.setInvert(false);
  P.setIntensity(0);
  P.setFont(fontSelect ? ASCII_Dobl : ASCII_Simp);
  miEfecto = random(2, (ARRAY_SIZE(catalogo)));

  for (uint8_t i = 1; i < ARRAY_SIZE(catalogo); i++)
  {
    catalogo[i].veloc *= CALIB_VELEFFECT;
  }

  P.setSpriteData(pacman1, W_PMAN1, F_PMAN1, pacman2, W_PMAN1, F_PMAN1);
  P.displayText("MAX72", PA_CENTER, catalogo[1].veloc, 2000, catalogo[1].efecto, catalogo[1].efecto);
  while (!P.displayAnimate())
    true;
  P.displayText("Conectando", PA_CENTER, catalogo[3].veloc, 0, catalogo[3].efecto, catalogo[3].efecto);
  while (!P.displayAnimate())
    true;

  conect_WiFi();
#if WEB
  if (!memModOTA)
    setupESPUI();
#endif

  leerDHT();
  modoHora();
  leer_serpApi(false);
  leerCreditoSerpApi();

  leer_CoinMarketCap();
  leer_OpenWeatherMap();

  String x = "  " + miIP.toString();
  P.displayText(x.c_str(), PA_CENTER, catalogo[3].veloc, 0, catalogo[3].efecto, catalogo[3].efecto);
  while (!P.displayAnimate())
    true;

#if OTA
  memModOTA ? funcOta() : (void)0;
#endif
  tAntSegdr = millis();
  tAntEfecto = millis();
  tAntUpdate = 0;
  modo = vFutbol; // Ultimo del carrusel

#if WEB
  cabInfo = String(resetReason(reason));
  webTiEstado(10);
#endif
  delay(1000);
}
void loop()
{
  static String dato;
  if ((millis() - tAntUpdate) > 2000)
  {
    sync();
    tAntUpdate = millis();

    if (!memEncendido)
      return;
    modoHora();
    modoClima();
  }

#if WEB
  cabInfo.length() > 0 ? webSiEstado() : (void)0;
  miAlarma.hor.est ? sonarAlarma() : (void)0;
#endif
#if MOD_MICRO
  !sonando ? microfono() : (void)0;
#endif

  // ------------------
  if (P.displayAnimate() && !sonando)
  {
    if (miMsj.estado)
    {
#if WEB
      WebMostrarMsj();
#endif
    }
    else
    {
      modoFutbol();
      modoCrypto();
      updateEfecto();

      switch (updatePantalla())
      {
      case vReloj: // ====> RELOJ
        switch (posiReloj)
        {
        case 0:
          guardarSPIFFS();
          formHora();
          dato = catVista[modo].datos;
          P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].veloc, 10, catalogo[miEfecto].efecto, PA_NO_EFFECT);
          tAntSegdr = millis();
          posiReloj = 1;
          runReloj = true;
          break;

        case 1:
          formHora();
          dato = catVista[modo].datos;
          P.displayText(dato.c_str(), PA_CENTER, 0, 600, PA_NO_EFFECT, PA_NO_EFFECT);

          rSeg = !rSeg;
          RljSegundero();
          break;

        case 2:
          formHora();
          dato = catVista[modo].datos;
          P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].veloc, 10, PA_NO_EFFECT, catalogo[miEfecto].efecto);

          rSeg = true;
          runReloj = false;
          break;
        }
        break;

      case vTemp: // ====> TEMPERATURA
        dato = catVista[modo].datos;
        P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].veloc, catVista[modo].duracion * 1000, catalogo[miEfecto].efecto, catalogo[miEfecto].efecto);
        break;

      case vHum: // ====> HUMEDAD
        dato = catVista[modo].datos;
        P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].veloc, catVista[modo].duracion * 1000, catalogo[miEfecto].efecto, catalogo[miEfecto].efecto);
        break;

      case vCript1: // ====> CRYPTO 1
        dato = catCrypto[0].datos;
        P.displayText(dato.c_str(), PA_CENTER, catCrypto[0].veloc, 0, catalogo[3].efecto, catalogo[3].efecto);
        break;

      case vCript2: // ====> CRYPTO 2
        dato = catCrypto[1].datos;
        P.displayText(dato.c_str(), PA_CENTER, catCrypto[1].veloc, 0, catalogo[3].efecto, catalogo[3].efecto);
        break;

      case vFutbol: // ====> FOOTBALL
        dato = mostrarFutbol();
        P.displayText(dato.c_str(), PA_CENTER, velocSA, 0, catalogo[3].efecto, catalogo[3].efecto);
        break;
#if ENCODER
      case vTimer: // ====> TEMPORIZADOR
        posiTempo = selectTimer(posiTempo);
        switch (posiTempo)
        {
        case 0:
          dato = obtenerTimer(99);
          P.displayText(dato.c_str(), PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
          break;

        case 1:
          dato = obtenerTimer(59);
          P.displayText(dato.c_str(), PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
          break;

        case 2:
          dato = contaTimer();
          P.displayText(dato.c_str(), PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
          break;

        case 3:
          digitalWrite(buzzer, LOW);
          modoTempo = false;
          modo = vVacio; // -1
          break;
        }
        break;
#endif
      default:
        P.displayText("Algo salio mal!", PA_CENTER, catalogo[3].veloc, 0, catalogo[3].efecto, catalogo[3].efecto);
        break;
      }
    }
  }

#if ENCODER
  if (btnEncoder() && !modoTempo)
  {
    P.displayReset();
    P.displayText(" ", PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
    modoTempo = true;
    posiTempo = 0;
    modo = vTimer;
  }
#endif
}
void sync()
{
  static bool syncHoy = false;
  uint8_t t = 4;

  if (miTiempo.hora == 0) // Resincronización (00:01 - 00:03)
  {
    if (miTiempo.minuto > 0 && miTiempo.minuto < t)
    {
      if (!syncHoy)
      {
        if (syncTime())
        {
          leer_serpApi(false);
          leerCreditoSerpApi();
          syncHoy = true;
          Serial.println("Sincronizacion diaria.");
        }
      }
    }

    syncHoy = (miTiempo.minuto == t) ? false : syncHoy;
  }
}

// ------------------------------------
void formHora()
{
  if (miTiempo.estado)
  {
    catVista[0].datos[2] = rSeg ? '_' : ':';
  }
}
String strHora(uint8_t &h, uint8_t &m)
{
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", h, m);
  return String(buffer);
}

void parsDateSA()
{
  time_t ahora;
  time(&ahora);
  struct tm info;
  localtime_r(&ahora, &info);
  time_t tCalculado = ahora;

  fxInfo.dia[0] = tolower(fxInfo.dia[0]);
  String txtDia = String(fxInfo.dia);

  if (txtDia == "today")
  {
    fxInfo.fechaDia = miTiempo.dia;
    fxInfo.fechaMes = miTiempo.mes;
  }
  else if (txtDia == "tomorrow")
  {
    tCalculado += 86400;
  }
  else
  {
    int diaObjetivo = indiceDia(txtDia);
    if (diaObjetivo != -1)
    {
      int hoySemana = info.tm_wday; // 0=sun .. 6=sat
      int diasFaltantes = (diaObjetivo - hoySemana + 7) % 7;
      if (diasFaltantes == 0)
        diasFaltantes = 7; // si es el mismo día, elegir la próxima semana

      tCalculado += (diasFaltantes * 86400);
    }
  }

  struct tm fechaFinal;
  localtime_r(&tCalculado, &fechaFinal);

  // Guardar en tu estructura
  fxInfo.fechaDia = fechaFinal.tm_mday;
  fxInfo.fechaMes = fechaFinal.tm_mon + 1;

  tradDiaSA(fxInfo.dia, sizeof(fxInfo.dia));
}
void parsTimeSA(const char *fuente)
{
  if (fuente == NULL || strlen(fuente) == 0)
    return;

  bool esPm = false;
  int h = 0, m = 0;

  if (sscanf(fuente, "%d:%d", &h, &m) >= 2)
  {
    esPm = (strstr(fuente, "P")) ? true : false;

    if (esPm)
    {
      if (h != 12)
      {
        h = h + 12;
      }
    }
    else
    {
      if (h == 12)
      {
        h = 0;
      }
    }

    fxInfo.fechaHora = h;
    fxInfo.fechaMinuto = m;
  }
}
void tradDiaSA(char *dato, size_t tam)
{
  for (int i = 0; i < 9; i++)
  {
    if (strcmp(dato, dic[i].fuent) == 0)
    {
      strlcpy(dato, dic[i].translate, tam);
      return;
    }
  }
}
int indiceDia(String &t)
{
  t.trim();
  if (t == "Sun")
    return 0;
  if (t == "Mon")
    return 1;
  if (t == "Tue")
    return 2;
  if (t == "Wed")
    return 3;
  if (t == "Thu")
    return 4;
  if (t == "Fri")
    return 5;
  if (t == "Sat")
    return 6;
  return -1;
}

void limpiarURL(char *str)
{
  char *i = str; // Puntero de lectura
  char *o = str; // Puntero de escritura

  while (*i)
  {
    uint8_t c = (uint8_t)*i;
    if (c == ' ')
    {
      *o++ = '+';
      i++;
    }
    else if (c == 0xC3)
    {
      i++;
      uint8_t proximo = (uint8_t)*i;

      switch (proximo)
      {
      case 0xA1:
        *o++ = 'a';
        break; // á
      case 0xA9:
        *o++ = 'e';
        break; // é
      case 0xAD:
        *o++ = 'i';
        break; // í
      case 0xB3:
        *o++ = 'o';
        break; // ó
      case 0xBA:
        *o++ = 'u';
        break; // ú
      case 0xB1:
        *o++ = 'n';
        break; // ñ
      case 0x81:
        *o++ = 'A';
        break; // Á
      case 0x89:
        *o++ = 'E';
        break; // É
      case 0x8D:
        *o++ = 'I';
        break; // Í
      case 0x93:
        *o++ = 'O';
        break; // Ó
      case 0x9A:
        *o++ = 'U';
        break; // Ú
      case 0x91:
        *o++ = 'N';
        break; // Ñ
      default:
        i--;
        break;
      }
      i++;
    }
    else
    {
      *o++ = *i++;
    }
  }
  *o = '\0';
}

partidoEst estadoAEnum(const char *est)
{
  if (strcmp(est, "Live") == 0)
    return LIVE;
  if (strcmp(est, "Half-time") == 0)
    return HT;
  if (strcmp(est, "Full-time") == 0)
    return FT;
  if (strcmp(est, "Extra Time") == 0)
    return ET;
  if (strcmp(est, "FT (P)") == 0)
    return Pens;
  if (strcmp(est, "Postponed") == 0)
    return PP;
  if (strcmp(est, "Abandoned") == 0)
    return Abd;
  return FX;
}
const char *estadoATexto(partidoEst est)
{
  switch (est)
  {
  case FX:
    return "FX";
  case LIVE:
    return "Live";
  case HT:
    return "HT";
  case FT:
    return "FT";
  case ET:
    return "ET";
  case Pens:
    return "Pens";
  case PP:
    return "PP";
  case Abd:
    return "Abd";
  case vacio:
    return "vacio";
  default:
    return "err";
  }
}

const char *resetReason(int reason)
{
  switch (reason)
  {
  case 0:
    return "Desconocido";
  case 1:
    return "Encendido (POWERON)";
  case 2:
    return "Reset externo (boton RESET)";
  case 3:
    return "Reinicio por software";
  case 4:
    return "Crash / panic";
  case 5:
    return "Watchdog interno";
  case 6:
    return "Watchdog de tareas";
  case 7:
    return "Watchdog";
  case 8:
    return "Deep sleep";
  case 9:
    return "Bajo voltaje (brownout)";
  case 10:
    return "Error SDIO";
  default:
    return "No identificado";
  }
}
void guardarSPIFFS()
{
  char buffer[120];
  snprintf(buffer, sizeof(buffer),
           "Millis: %lu, Free: %dKb, Min: %dKb, Max Alloc: %dKb |\n",
           millis(),
           heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024,           // Libre actual
           ESP.getMinFreeHeap() / 1024,                               // Minimo historico
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024); // Fragmento mas grande
  Serial.printf("%s", buffer);

  File file = SPIFFS.open("/dato.txt", FILE_APPEND);
  if (!file)
  {
    Serial.println("Error abriendo archivo para escritura");
  }

  if (file.size() > 500)
  {
    file.close();
    SPIFFS.remove("/dato.txt");
    file = SPIFFS.open("/dato.txt", FILE_WRITE);
  }

  if (!file.print(buffer))
  {
    Serial.println("Error escribiendo archivo");
  }
  file.close();
}
void imprimirSPIFFS()
{
  File file = SPIFFS.open("/dato.txt", FILE_READ);
  if (!file)
  {
    Serial.println("Error abriendo archivo");
    return;
  }

  Serial.println("LOGS ANTERIOR:");
  while (file.available())
  {
    Serial.write(file.read());
  }
  file.close();
}
void imprimir()
{
  Serial.println("\n--- DATOS (SERPAPI) ---");
  const char *textoEstado = estadoATexto(fxInfo.estado);
  Serial.printf("Estado: %s\n", textoEstado);
  Serial.printf("Partido: %s %s - %s %s, %s\n", fxInfo.local, fxInfo.sLocal, fxInfo.sVisita, fxInfo.visita, fxInfo.torneo);
  Serial.printf("Minuto: +%02d min.\n", fxInfo.min);
  Serial.printf("Fecha: %s, %02d/%02d - %02d:%02d\n", fxInfo.dia, fxInfo.fechaDia, fxInfo.fechaMes, fxInfo.fechaHora, fxInfo.fechaMinuto);
}