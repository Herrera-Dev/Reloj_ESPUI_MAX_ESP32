
// ESPAsyncWebServer 3.1.0

#define DEBUG 1
#define RTC 0
#define SERVIDOR 1

#if SERVIDOR
#include <ESPUI.h>
#endif

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include "RTClib.h"
#include <Preferences.h>
Preferences memoria;

#include "SPI.h"
#include <MD_Parola.h>

#include <DHT.h>
#include "fontText.h"
#include "caracteres.h"

String ssid;
String password;
char miIp[40];
const char *mdns = "espui"; // Web: http://espui.local/
const char *hostname = "ESPUI-MAX";
IPAddress apIP(192, 168, 6, 1);
const char *ntp = "pool.ntp.org";

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

#define DATA_PIN 23 // or MOSI
#define CS_PIN 5    // or SS (cambiable a otro gpin)
#define CLK_PIN 18  // or SCK

MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
// Grados = &, Reloj = #, Espacion = _

struct tipoEfecto
{
  String nombre;
  textEffect_t efecto;
  uint16_t velocidad;
  uint16_t pausa;
};
tipoEfecto catalogo[] = {
    {"PRINT", PA_PRINT, 1, 1},
    {"SPRITE", PA_SPRITE, 6, 1},
    {"SCROLL_U", PA_SCROLL_UP, 5, 1},
    {"SCROLL_L", PA_SCROLL_LEFT, 5, 1},
    {"MESH", PA_MESH, 20, 1},
    {"FADE", PA_FADE, 20, 1},
    {"RANDOM", PA_RANDOM, 3, 1},
    {"WIPE_C", PA_WIPE_CURSOR, 4, 1},
    {"SCAN_H", PA_SCAN_HORIZ, 4, 1},
    {"SCAN_VX", PA_SCAN_VERTX, 3, 1},
    {"OPENING_C", PA_OPENING_CURSOR, 4, 1},
    {"CLOSING", PA_CLOSING, 3, 1},
    {"SCROLL_UL", PA_SCROLL_UP_LEFT, 7, 1},
    {"SCROLL_DL", PA_SCROLL_DOWN_LEFT, 7, 1},
    {"GROW_U", PA_GROW_UP, 7, 1},
};

bool inicio = true;
bool enLinea = false;
bool tipoFont = true;
unsigned long tAntUpdate;

// MICROFONO
#define micro 35
byte flanco = 0;

// BUZZER
#define buzzer 33
bool sonando = false;
bool silenciar = false;
bool mensAlarm = true;

// DHT22
#define sensor_temp 15
#define Tipo DHT22
DHT dht(sensor_temp, Tipo);
const float calibTemp = -0.60;

// RTC
RTC_DS3231 rtc;
char miHoraV[6]; // 00_00
char miHoraAnt[6];
bool rInicio = true;
bool rFin = true;
bool segundero = true;
unsigned long tRtcAnt;

#define A_CLK 19
#define B_DT 4
#define BTN_SW 17
int lastCLKState;
int currentCLKState;
int mins = 0;
int secs = 0;
int valor = 0;
int estadTemp = 0;
bool modoTemp = false;
unsigned long duracion = 0;
unsigned long tiempoInicio = 0;

// MOSTRAR
byte modo = 0;
unsigned long tModoAnt;

struct modulosV
{
  char datos[6];
  byte tiempo;
  bool estado;
};

modulosV catalogoVista[] = {
    {"", 40, true}, // Reloj
    {"", 10, true}, // Temperatura
    {"", 10, true}  // Humedad
};

// EFECTOS
byte miEfecto = 2;
unsigned long tEfectoAnt;

// ========================================
String cabInfo = "";
unsigned long tWebCabAnt;

bool memEncendido = true; // Encendido o Apagado
bool memRelojUDP = true;  // 0=RTC, 1=UDP
bool memRelojActu = true; // Actualizar: manual=0, automatico=1
String memHora = "00";
String memMin = "00";

bool memAnimAle = true;    // EPROM Aleatorio=1, No aleatorio=0
int memAnimEfecto = 1;     // EPROM Select: efecto
byte memAnimDuracion = 10; // EPROM Duracion: minutos

bool memAlarActiv = false; // EPROM Activado=1, No activado=0
bool memAlarRep = false;   // EPROM Repetir=1, No repetir=0
String miAlarma;           // EPROM Hora alarma

char miMensaje[50];
bool memMensaEst = false;    // Mensaje: activar o desactivar
bool memMensaTipo = false;   // Recorrer: Vertical=1, Horizontal=0
bool memMensaIndefi = false; // Mensaje: Indefinido=1, No Indefinido=0
byte memMensaDura = 2;       // Mensaje: duracion en pantalla
byte memMensaVeloc = 20;     // Mensaje: velocidad efecto
byte memMensaPausa = 0;      // Mensaje: pausar en pantalla
bool memModAP = false;
unsigned long tMensaAnt;

static const String deshabilitar = "background-color: #bbb; border-bottom: #999 3px solid;";
int cabEstado, animEstado, alarmEstad, advertencia;
int modulHora, modulTemp, modulHum;
int horaEstado, horaTipo, horaGuardar, horaDuracionR, horaDuracionT, horaDuracionH, horaBrillo, horaEst, tempEst, humeEst;
int animAleatorio, animAnimacion, animDuracion;
int alarmRep, alarmHor, alarmMin, alarmGuard, alarmSilen;
int mensTipo, mensText, mensDura, mensVelo, mensPausa, mensGuard;
int modAp, wifi, pass, wifiGuard, fontTipo;

// ========================================
void setup()
{
  Wire.begin();
  dht.begin();
  Serial.begin(115200);
  pinMode(buzzer, OUTPUT);
  pinMode(micro, INPUT);
  randomSeed(analogRead(A0));
  getDatos();

  pinMode(A_CLK, INPUT);
  pinMode(B_DT, INPUT);
  pinMode(BTN_SW, INPUT);
  lastCLKState = digitalRead(A_CLK);

  // MAX7219
  delay(2500);
  P.begin();
  P.setInvert(false);
  P.setIntensity(0);
  P.setFont(tipoFont ? ASCII_Dobl : ASCII_Simp);
  for (uint8_t i = 1; i < ARRAY_SIZE(catalogo); i++)
  {
    catalogo[i].velocidad *= 4;
  }

// RTC
#if RTC
  if (!rtc.begin())
  {
    Serial.println(F("No se encontro el RTC"));
  }
#endif

  conexionWifi();
  WiFi.setSleep(false);

  interfazWeb();
  leerTempDHT();
  leerHumDHT();
  tModoAnt = millis();
  tEfectoAnt = millis();
  tRtcAnt = millis();
  tAntUpdate = millis();

  Serial.println(F("=============="));
  switch (esp_reset_reason())
  {
  case ESP_RST_POWERON:
    Serial.println("Conectado a la alimentacion");
    P.setSpriteData(pacman1, W_PMAN1, F_PMAN1, pacman2, W_PMAN1, F_PMAN1);
    P.displayText("MAX72", PA_CENTER, catalogo[1].velocidad, catalogo[1].pausa * 3000, catalogo[1].efecto, catalogo[1].efecto);
    break;
  }
}

void conexionWifi()
{
  if (!memModAP) // MODO STA
  {
    int connect_timeout = 30;
    WiFi.setHostname(hostname);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    Serial.print(F("\nWifi conectando"));
    while (WiFi.status() != WL_CONNECTED && connect_timeout > 0) // MODO CLIENTE
    {
      delay(300);
      Serial.print(F("."));
      connect_timeout--;
    }

    if (connect_timeout > 0)
    {
      delay(1000);
      WiFiClient client;
      if (client.connect("www.google.com", 80))
      {
        configTime((-4 * 3600), 0, ntp);
        time_t now = time(nullptr);
        while (now < 24 * 3600)
        {
          delay(100);
          now = time(nullptr);
        }
      }
      else
      {
#if RTC
        memRelojUDP = false;
#endif
      }
      client.stop();
    }
    else
    {
      modoAP();
    }
  }
  else // MODO AP
  {
    modoAP();
  }

  String dir = "";
  if (!MDNS.begin(mdns))
  {
    dir = "E: MDNS - ";
    Serial.println("Error: MDNS");
  }

  dir += (WiFi.getMode() == WIFI_AP) ? "AP: " : "Station: ";
  dir += (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  dir.toCharArray(miIp, dir.length() + 1);

  Serial.println("\n" + dir);
}

void modoAP()
{
  Serial.print("\n\nPunto de acceso");
  memRelojUDP = false;

  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(hostname);

  int connect_timeout = 10;
  do
  {
    delay(400);
    Serial.print(".");
    connect_timeout--;
  } while (connect_timeout);
}

void interfazWeb()
{
#if SERVIDOR
  static char estiloSensores[38];
  strcpy(estiloSensores, "background-color: unset; width: 100%;");
  static char estiloSinColor[45];
  strcpy(estiloSinColor, "background-color: unset; margin-bottom: 0px;");
  static char estiloEst[41];
  strcpy(estiloEst, "color: #e74c3c; background-color: unset;");
  static char estiloVist[23];
  strcpy(estiloVist, "top: 7px; left: 10px;");
  static char sinMarginB[22];
  strcpy(sinMarginB, "margin-bottom: 1px;");
  // static String estiloMod = "text-shadow: 2px 2px 2px rgba(0, 0, 0, 0.3); font-size: 60px; background-color: unset; padding-right: 0.1em; padding-left o.1em;";
  static String estiloMod = "text-shadow: 2px 2px 2px rgba(0, 0, 0, 0.3); font-size: 60px; background-color: unset; padding: 1px; margin: 1px;";

  auto tab0 = ESPUI.addControl(Tab, "", "Modulos");
  auto tab1 = ESPUI.addControl(Tab, "", "Reloj");
  auto tab2 = ESPUI.addControl(Tab, "", "Alarma");
  auto tab3 = ESPUI.addControl(Tab, "", "Animacion");
  auto tab4 = ESPUI.addControl(Tab, "", "Mensaje");
  auto tab5 = ESPUI.addControl(Tab, "", "Wifi");
  // ESTADOS--------------------------------------
  String info = memAnimAle ? "Animacion: Aleatorio" : "Animacion: " + catalogo[memAnimEfecto].nombre;
  animEstado = ESPUI.addControl(Label, miIp, info, Turquoise);
  alarmEstad = ESPUI.addControl(Label, "", miAlarma, Turquoise, animEstado);
  cabEstado = ESPUI.addControl(Label, "", "", Turquoise, animEstado);
  ESPUI.addControl(Button, "Boton", "Reiniciar ESP32", None, animEstado, espReiniciar);

  ESPUI.setElementStyle(animEstado, String(estiloSinColor));
  ESPUI.setElementStyle(alarmEstad, String(estiloSinColor));
  ESPUI.setElementStyle(cabEstado, String(estiloSinColor));

  // MODULOS-------------------------------------
  modulHora = ESPUI.addControl(Label, "Reloj", "00:00", Peterriver, tab0);
  modulTemp = ESPUI.addControl(Label, "Temperatura", "00.0°C", Peterriver, tab0);
  modulHum = ESPUI.addControl(Label, "Humedad", "00.0%", Peterriver, tab0);
  ESPUI.setElementStyle(modulHora, estiloMod);
  ESPUI.setElementStyle(modulTemp, estiloMod);
  ESPUI.setElementStyle(modulHum, estiloMod);

  // RELOJ-------------------------------------
  horaEstado = ESPUI.addControl(Switcher, "Encender", "1", Wetasphalt, tab1, webRelojEstado); // Estado Encendido o Apagado
  horaTipo = ESPUI.addControl(Select, "Hora", "", Emerald, tab1, webRelojTipo);               // Tipo de reloj rtc o ntp
  ESPUI.addControl(Option, "Modulo RTC", "O", Alizarin, horaTipo);
  ESPUI.addControl(Option, "Servidor NTP", "1", Alizarin, horaTipo);
  ESPUI.updateSelect(horaTipo, String(memRelojUDP));

  horaBrillo = ESPUI.addControl(Slider, "Brillo", "0", Dark, tab1, webRelojBrillo);
  ESPUI.addControl(Min, "", "0", None, horaBrillo);
  ESPUI.addControl(Max, "", "10", None, horaBrillo);

  horaGuardar = ESPUI.addControl(Button, "Modulo RTC DS3231", "Actualizar Datos", Alizarin, tab1, webRelojGuardar); // Guardar nueva hora

  ESPUI.addControl(Separator, "Tiempo de Visualizacion", "", None, tab1); // Duracion para mostrar los datos

  horaDuracionR = ESPUI.addControl(Number, "Visualizacion (Seg)", String(catalogoVista[0].tiempo), Peterriver, tab1, webRelojDuracion);
  horaEst = ESPUI.addControl(Switcher, "", String(catalogoVista[0].estado), Wetasphalt, horaDuracionR, webReEst);
  ESPUI.setElementStyle(horaEst, estiloVist);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Hora", None, horaDuracionR, webRelojDuracion), estiloSensores);
  ESPUI.addControl(Min, "", "3", None, horaDuracionR);
  ESPUI.addControl(Max, "", "600", None, horaDuracionR);

  horaDuracionT = ESPUI.addControl(Number, "", String(catalogoVista[1].tiempo), Peterriver, horaDuracionR, webRelojDuracion);
  tempEst = ESPUI.addControl(Switcher, "", String(catalogoVista[1].estado), Wetasphalt, horaDuracionR, webTeEst);
  ESPUI.setElementStyle(tempEst, estiloVist);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Temperatura", None, horaDuracionR, webRelojDuracion), estiloSensores);
  ESPUI.addControl(Min, "", "3", None, horaDuracionT);
  ESPUI.addControl(Max, "", "600", None, horaDuracionT);

  horaDuracionH = ESPUI.addControl(Number, "", String(catalogoVista[2].tiempo), Peterriver, horaDuracionR, webRelojDuracion);
  humeEst = ESPUI.addControl(Switcher, "", String(catalogoVista[2].estado), Wetasphalt, horaDuracionR, webHuEst);
  ESPUI.setElementStyle(humeEst, estiloVist);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Humedad", None, horaDuracionR, webRelojDuracion), estiloSensores);
  ESPUI.addControl(Min, "", "3", None, horaDuracionH);
  ESPUI.addControl(Max, "", "600", None, horaDuracionH);

  ESPUI.setElementStyle(horaDuracionR, String(sinMarginB));
  ESPUI.setElementStyle(horaDuracionT, String(sinMarginB));
  ESPUI.setElementStyle(horaDuracionH, String(sinMarginB));

  // ANIMACION---------------------------------
  animAleatorio = ESPUI.addControl(Switcher, "Animacion: Aleatorio", String(memAnimAle), Wetasphalt, tab3, webAnimAleatorio); // Aleatorio

  static String animaciones[ARRAY_SIZE(catalogo)]; // Elegir una animacion
  for (int i = 0; i < ARRAY_SIZE(catalogo); i++)
  {
    animaciones[i] = String(catalogo[i].nombre);
  }

  animAnimacion = ESPUI.addControl(Select, "Animacion", "", Peterriver, tab3, webAnimAnimacion);
  for (static int i = 0; i < ARRAY_SIZE(catalogo); i++)
  {
    ESPUI.addControl(Option, animaciones[i].c_str(), String(i), None, animAnimacion);
  }

  if (memAnimAle)
  {
    ESPUI.setEnabled(animAnimacion, false);
    ESPUI.setPanelStyle(animAnimacion, deshabilitar);
  }
  else
  {
    ESPUI.updateSelect(animAnimacion, String(memAnimEfecto));
    ESPUI.setEnabled(animDuracion, false);
    ESPUI.setPanelStyle(animDuracion, deshabilitar);
  }

  animDuracion = ESPUI.addControl(Slider, "Duracion (Min)", String(memAnimDuracion), Dark, tab3, webAnimDuracion);
  ESPUI.addControl(Min, "", "3", None, animDuracion);
  ESPUI.addControl(Max, "", "120", None, animDuracion);

  // ALARMA------------------------------------
  ESPUI.addControl(Separator, "Alarma Formato: 24H", "", None, tab2);
  alarmHor = ESPUI.addControl(Number, "Alarma", "00", Peterriver, tab2, webAlarmHora); // Hora de alarma
  ESPUI.addControl(Min, "", "0", None, alarmHor);
  ESPUI.addControl(Max, "", "24", None, alarmHor);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Hora", None, alarmHor, webAlarmHora), estiloSensores);
  alarmMin = ESPUI.addControl(Number, "", "00", Peterriver, alarmHor, webAlarmHora); // Minuto de alarma
  ESPUI.addControl(Min, "", "0", None, alarmMin);
  ESPUI.addControl(Max, "", "59", None, alarmMin);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Minuto", None, alarmHor, webAlarmHora), estiloSensores);
  ESPUI.setElementStyle(alarmHor, String(sinMarginB));
  ESPUI.setElementStyle(alarmMin, String(sinMarginB));

  alarmRep = ESPUI.addControl(Switcher, "Repetir", String(memAlarRep), Wetasphalt, tab2, webAlarmRepet); // Repetir alarma ON o OFF
  alarmGuard = ESPUI.addControl(Button, "Alarma", "", Alizarin, tab2, webAlarmGuard);                    // Guardar o cancelar la alarma
  if (memAlarActiv)
  { // Alarma activo
    String n = memAlarRep ? " Repetir" : " ";
    n = "Alarma: " + miAlarma + n;
    ESPUI.print(alarmEstad, n);
    ESPUI.updateControlValue(alarmGuard, String("Cancelar alarma").c_str());
  }
  else
  { // Alarma no activo
    ESPUI.print(alarmEstad, String("Alarma: Ninguno"));
    ESPUI.updateControlValue(alarmGuard, String("Guardar alarma").c_str());
  }

  alarmSilen = ESPUI.addControl(Button, "Silenciar alarma", "Aceptar", Emerald, tab2, webAlarmSilen);
  if (!memAlarActiv)
  {
    ESPUI.setEnabled(alarmSilen, false);
    ESPUI.setPanelStyle(alarmSilen, deshabilitar);
  }

  // MENSAJE ------------------------------------
  mensText = ESPUI.addControl(Text, "Mensaje", "", Sunflower, tab4, webMensTexto);

  mensTipo = ESPUI.addControl(Select, "Tipo", "", Emerald, tab4, webMensTipo);
  ESPUI.addControl(Option, "Horizontal", "O", Alizarin, mensTipo);
  ESPUI.addControl(Option, "Vertical", "1", Alizarin, mensTipo);

  ESPUI.addControl(Switcher, "Duracion Indefinido", String(memMensaIndefi), Wetasphalt, tab4, webMensIndefi);

  mensDura = ESPUI.addControl(Slider, "Duracion (Min)", "2", Dark, tab4, webMensDuracion);
  ESPUI.addControl(Min, "", "1", None, mensDura);
  ESPUI.addControl(Max, "", "60", None, mensDura);

  mensVelo = ESPUI.addControl(Slider, "Animacion", "20", Dark, tab4, webMensVelocidad);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Velocidad (ms)", None, mensVelo, webMensVelocidad), estiloSensores);
  ESPUI.addControl(Min, "", "10", None, mensVelo);
  ESPUI.addControl(Max, "", "150", None, mensVelo);

  mensPausa = ESPUI.addControl(Slider, "", "0", Dark, mensVelo, webMensPausa);
  ESPUI.setElementStyle(ESPUI.addControl(Label, "", "Pausa (Seg)", None, mensVelo, webMensPausa), estiloSensores);
  ESPUI.addControl(Min, "", "0", None, mensPausa);
  ESPUI.addControl(Max, "", "60", None, mensPausa);
  ESPUI.setElementStyle(mensVelo, String(sinMarginB));
  ESPUI.setElementStyle(mensPausa, String(sinMarginB));

  mensGuard = ESPUI.addControl(Button, "Mensaje", "Enviar", Alizarin, tab4, webMensEnviar);
  modAp = ESPUI.addControl(Switcher, "Modo Access Point", String(memModAP), Wetasphalt, tab5, webModoAP);
  wifi = ESPUI.addControl(Text, "SSID", "", Dark, tab5, webssid);
  pass = ESPUI.addControl(Text, "Contraseña", "", Dark, tab5, webpass);
  wifiGuard = ESPUI.addControl(Button, "Actualizar Wifi", "Guardar", Alizarin, tab5, webWifiGuard);

  fontTipo = ESPUI.addControl(Switcher, "Simple <-- Font --> Doble", String(tipoFont), Wetasphalt, tab3, webFont);

  // ESPUI.begin("ESPUI Control", "username", "password") //Seguridad basica.
  ESPUI.begin("ESP32UI MAX7219");
#endif
}

void getDatos()
{
  memoria.begin("web", false);
  delay(20);
  catalogoVista[0].tiempo = memoria.getInt("tiem0", 10);
  catalogoVista[1].tiempo = memoria.getInt("tiem1", 10);
  catalogoVista[2].tiempo = memoria.getInt("tiem2", 10);

  catalogoVista[0].estado = memoria.getBool("horEst", true);
  catalogoVista[1].estado = memoria.getBool("temEst", true);
  catalogoVista[2].estado = memoria.getBool("humEst", true);
  memRelojUDP = memoria.getBool("rTipo", true);
  memAnimAle = memoria.getBool("anAle", true);
  memAnimEfecto = memoria.getInt("anEfec", 1);
  memAnimDuracion = (byte)memoria.getInt("anDura", 10);
  memAlarActiv = memoria.getBool("alAct", false);
  memAlarRep = memoria.getBool("alRep", false);
  miAlarma = memoria.getString("miAlarm", "");
  memModAP = memoria.getBool("modoAP", false);
  ssid = memoria.getString("ssid", "null");
  password = memoria.getString("pass", "null");
  tipoFont = memoria.getBool("tipFont", true);
  memoria.end();
}
void setBool(const char *d, bool v)
{
  memoria.begin("web", false);
  memoria.putBool(d, v);
  memoria.end();
}
void setInt(const char *d, int v)
{
  memoria.begin("web", false);
  memoria.putInt(d, v);
  memoria.end();
}
void setString(const char *d, String v)
{
  memoria.begin("web", false);
  memoria.putString(d, v);
  memoria.end();
}

// ========================================
#if SERVIDOR
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

void webRelojTipo(Control *sender, int value)
{ // RTC o NTP
  test(sender, value);
  memRelojUDP = sender->value.toInt();
  setBool("rTipo", memRelojUDP);
}

void webRelojBrillo(Control *sender, int type)
{ // Cambiar brillo
  test(sender, type);
  P.setIntensity(sender->value.toInt());
}

void webRelojGuardar(Control *sender, int type)
{ // Guardar nueva hora RTC
  test(sender, type);
  switch (type)
  {
  case B_UP:
    if (!rtc.lostPower())
    {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo))
      {
        rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
        cabInfo = "RTC: " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
        tWebCabAnt = millis();
      }
      else
      {
        cabInfo = "No se pudo actualizar la hora.";
        tWebCabAnt = millis();
      }
    }
    else
    {
      cabInfo = "No se encuentra el RTC.";
      tWebCabAnt = millis();
    }
    break;
  }
}

void webRelojDuracion(Control *sender, int value)
{ // Cambiar tiempos de Reloj, Temp y Hum.
  test(sender, value);
  switch (sender->id)
  {
  case 23:
    catalogoVista[0].tiempo = sender->value.toInt();
    setInt("tiem0", catalogoVista[0].tiempo);
    break;

  case 28:
    catalogoVista[1].tiempo = sender->value.toInt();
    setInt("tiem1", catalogoVista[1].tiempo);
    break;

  case 33:
    catalogoVista[2].tiempo = sender->value.toInt();
    setInt("tiem2", catalogoVista[2].tiempo);
    break;

  default:
#if DEBUG
    Serial.println(F("El id no coincide."));
#endif
    cabInfo = "El ID no coincide";
    tWebCabAnt = millis();
    break;
  }
}

void webReEst(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    catalogoVista[0].estado = true;
    setBool("horEst", catalogoVista[0].estado);
    break;
  case S_INACTIVE:
    if (catalogoVista[1].estado || catalogoVista[2].estado)
    {
      catalogoVista[0].estado = false;
      setBool("horEst", catalogoVista[0].estado);
    }
    else
    {
      ESPUI.updateSwitcher(horaEst, true);
      cabInfo = "No desactivar todo.";
      tWebCabAnt = millis();
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
    catalogoVista[1].estado = true;
    setBool("temEst", catalogoVista[1].estado);
    break;
  case S_INACTIVE:
    if (catalogoVista[0].estado || catalogoVista[2].estado)
    {
      catalogoVista[1].estado = false;
      setBool("temEst", catalogoVista[1].estado);
    }
    else
    {
      ESPUI.updateSwitcher(tempEst, true);
      cabInfo = "No desactivar todo.";
      tWebCabAnt = millis();
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
    catalogoVista[2].estado = true;
    setBool("humEst", catalogoVista[2].estado);
    break;
  case S_INACTIVE:
    if (catalogoVista[0].estado || catalogoVista[1].estado)
    {
      catalogoVista[2].estado = false;
      setBool("humEst", catalogoVista[2].estado);
    }
    else
    {
      ESPUI.updateSwitcher(humeEst, true);
      cabInfo = "No desactivar todo.";
      tWebCabAnt = millis();
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
    ESPUI.print(animEstado, String("Animacion: Aleatorio"));
    ESPUI.setEnabled(animAnimacion, false);
    ESPUI.setPanelStyle(animAnimacion, deshabilitar);
    ESPUI.setEnabled(animDuracion, true);
    ESPUI.setPanelStyle(animDuracion, ";");
    memAnimAle = true;
    setBool("anAle", memAnimAle);
    break;

  case S_INACTIVE:
    String n = "Animacion: " + catalogo[memAnimEfecto].nombre;
    ESPUI.print(animEstado, n);
    ESPUI.updateSelect(animAnimacion, String(memAnimEfecto));
    ESPUI.setEnabled(animAnimacion, true);
    ESPUI.setPanelStyle(animAnimacion, ";");
    ESPUI.setEnabled(animDuracion, false);
    ESPUI.setPanelStyle(animDuracion, deshabilitar);
    memAnimAle = false;
    setBool("anAle", memAnimAle);
    break;
  }
}

void webAnimAnimacion(Control *sender, int value)
{ // Elegir animacion
  test(sender, value);
  memAnimEfecto = sender->value.toInt();
  ESPUI.print(animEstado, ("Animacion: " + catalogo[memAnimEfecto].nombre));
  setInt("anEfec", memAnimEfecto);
}

void webAnimDuracion(Control *sender, int value)
{
  test(sender, value);
  memAnimDuracion = sender->value.toInt();
  setInt("anDura", memAnimDuracion);
}

// --------------
void webAlarmRepet(Control *sender, int value)
{
  test(sender, value);
  memAlarRep = sender->value.toInt();
  setBool("alRep", memAlarRep);
}

void webAlarmHora(Control *sender, int value)
{
  test(sender, value);
  String n = sender->value;
  if (n.length() == 1)
  {
    n = '0' + n;
  }

  switch (sender->id)
  {
  case 59:
    memHora = n;
    break;

  case 63:
    memMin = n;
    break;

  default:
    cabInfo = "El ID no coincide";
    tWebCabAnt = millis();
    break;
  }
}

void webAlarmGuard(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    test(sender, type);

    String n = ESPUI.getControl(alarmGuard)->value;
    if (n.equals("Guardar alarma"))
    { // nueva alarma
      memAlarActiv = true;
      mensAlarm = true;
      miAlarma = memHora + ":" + memMin;
      String n = memAlarRep ? " Repetir" : " ";
      n = "Alarma: " + miAlarma + n;

      setBool("alAct", memAlarActiv);
      setBool("alRep", memAlarRep);
      setString("miAlarm", miAlarma);

#if SERVIDOR
      ESPUI.updateControlValue(alarmEstad, n);
      ESPUI.updateControlValue(alarmGuard, String("Cancelar alarma").c_str());
      ESPUI.setEnabled(alarmSilen, true);
      ESPUI.setPanelStyle(alarmSilen, ";");
#endif
    }
    else
    { // cancelar alarma
      miAlarma = "";
      sonando = true;
      memAlarRep = false;
    }
    break;
  }
}

void webAlarmSilen(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    test(sender, type);
    silenciar = true;
    break;
  }
}

// --------------
void webMensTipo(Control *sender, int value)
{
  test(sender, value);
  memMensaTipo = sender->value.toInt();
  if (memMensaTipo)
  {
    if (memMensaPausa == 0)
    {
      ESPUI.updateSlider(mensPausa, 2);
      memMensaPausa = 2;
    }
  }
  else
  {
    ESPUI.updateSlider(mensPausa, 0);
    memMensaPausa = 0;
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
    memMensaIndefi = true;
    ESPUI.updateVisibility(mensDura, false);
    break;
  case S_INACTIVE:
    memMensaIndefi = false;
    tMensaAnt = millis();
    ESPUI.updateVisibility(mensDura, true);
    break;
  }
}

void webMensDuracion(Control *sender, int value)
{
  test(sender, value);
  memMensaDura = sender->value.toInt();
}

void webMensVelocidad(Control *sender, int value)
{
  test(sender, value);
  memMensaVeloc = sender->value.toInt();
}

void webMensPausa(Control *sender, int value)
{
  test(sender, value);
  memMensaPausa = sender->value.toInt();
}

void webMensEnviar(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    test(sender, type);
    if (strlen(miMensaje) > 0)
    {
      memMensaEst = !memMensaEst;
      if (memMensaEst)
      {
        ESPUI.updateControlValue(mensGuard, String("Cancelar").c_str());
        tMensaAnt = millis();
      }
      else
      {
        ESPUI.updateControlValue(mensGuard, String("Enviar").c_str());
        ESPUI.updateControlValue(mensText, String(""));
        // miMensaje = "";
      }
      break;
    }
    else
    {
#if DEBUG
      Serial.println("Mensaje vacio.");
#endif
      cabInfo = "Mensaje vacio.";
      tWebCabAnt = millis();
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

void webssid(Control *sender, int value)
{
  test(sender, value);
  ssid = sender->value;
}

void webpass(Control *sender, int value)
{
  test(sender, value);
  password = sender->value;
}

void webWifiGuard(Control *sender, int value)
{
  test(sender, value);
  if (ssid.length() > 1 && password.length() > 1)
  {
    // setString("ssid", ESPUI.getControl(wifi)->value);
    setString("ssid", ssid);
    delay(2);
    // setString("pass", ESPUI.getControl(pass)->value);
    setString("pass", password);
    delay(1000);
    ESP.restart();
  }
}

void webFont(Control *sender, int value)
{
  test(sender, value);
  switch (value)
  {
  case S_ACTIVE:
    P.setFont(ASCII_Dobl);
    setBool("tipFont", true);
    break;
  case S_INACTIVE:
    P.setFont(ASCII_Simp);
    setBool("tipFont", false);
    break;
  }
}

// ========================================
void espReiniciar(Control *sender, int type)
{
  switch (type)
  {
  case B_UP:
    ESP.restart();
    break;
  }
}
void test(Control *sender, int type)
{
#if DEBUG
  Serial.println("------------------");
  Serial.print("ID:");
  Serial.print(sender->id);
  Serial.print(" - Type:");
  Serial.print(type);
  Serial.print(" - '");
  Serial.print(sender->label);
  Serial.print("' = ");
  Serial.println(sender->value);
#endif
}
#endif

// ========================================
#if SERVIDOR
void webSiEstado()
{
  if (millis() - tWebCabAnt < 4 * 1000)
  {
    ESPUI.getControl(animEstado)->color = ControlColor::Carrot;
    ESPUI.updateControl(animEstado);
    ESPUI.print(cabEstado, cabInfo);
  }
  else
  {
    ESPUI.getControl(animEstado)->color = ControlColor::Turquoise;
    ESPUI.updateControl(animEstado);
    cabInfo = "";
    ESPUI.print(cabEstado, cabInfo);
  }
}

void WebMostrarMsj()
{
  if (memMensaIndefi)
  {
    P.displayText(miMensaje, PA_CENTER, memMensaVeloc, memMensaPausa * 1000, catalogo[3].efecto, catalogo[3].efecto);
  }
  else
  {

    if ((millis() - tMensaAnt) < (memMensaDura * 60000))
    {
      if (memMensaTipo)
      {
        P.displayText(miMensaje, PA_CENTER, memMensaVeloc, memMensaPausa * 1000, catalogo[2].efecto, catalogo[2].efecto);
      }
      else
      {
        P.displayText(miMensaje, PA_CENTER, memMensaVeloc, memMensaPausa * 1000, catalogo[3].efecto, catalogo[3].efecto);
      }
    }
    else
    {
      // miMensaje = "";
      memMensaEst = false;
      ESPUI.updateControlValue(mensGuard, String("Enviar").c_str());
      ESPUI.print(mensText, "");
    }
  }
}

void sonarAlarma()
{
  if (miAlarma.equals(String(catalogoVista[0].datos)))
  { // Alarma sonando
    sonando = true;

    if (mensAlarm)
    {
      P.displayReset();
      P.displayText(" ", PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
      mensAlarm = false;
    }

    if (P.displayAnimate())
    {
      P.displayText("<< Alarma >>", PA_CENTER, (memMensaVeloc * 3), 0, catalogo[3].efecto, catalogo[3].efecto);
    }

    silenciar = (digitalRead(BTN_SW) == LOW) ? true : silenciar; // Silenciar
    delay(100);
    if (silenciar)
    {
      digitalWrite(buzzer, LOW);
    }
    else
    {
      digitalWrite(buzzer, HIGH);
#if DEBUG
      cabInfo = String("sonando...");
      tWebCabAnt = millis();
#endif
    }
  }
  else if (sonando)
  { // Alarma despues de sonar o desactivado
    sonando = false;
    silenciar = false;
    mensAlarm = true;
    digitalWrite(buzzer, LOW);

    if (!memAlarRep)
    {
      miAlarma = "";
      memAlarActiv = false;
      setBool("alAct", memAlarActiv);
      setBool("alRep", memAlarRep);
      setString("miAlarm", miAlarma);

#if SERVIDOR
      ESPUI.updateControlValue(alarmRep, String(memAlarRep));
      ESPUI.print(alarmEstad, String("Alarma: Ninguno"));
      ESPUI.updateControlValue(alarmGuard, String("Guardar alarma").c_str());
      ESPUI.setEnabled(alarmSilen, false);
      ESPUI.setPanelStyle(alarmSilen, deshabilitar);
#endif
    }
  }
}
#endif

void modoRtc()
{
#if RTC
  if (millis() - tRtcAnt > 2000)
  {
    if (!rtc.lostPower())
    {
      DateTime tiempo = rtc.now();
      char formato[] = "hh:mm";
      String hora = tiempo.toString(formato);
      hora.toCharArray(catalogoVista[0].datos, 6); // 00:00

      int result = strcmp(catalogoVista[0].datos, miHoraAnt);
      if (result != 0)
      {
        strcpy(formato, "hh_mm");
        hora = tiempo.toString(formato);
        hora.toCharArray(miHoraV, 6); // 00_00
        strcpy(miHoraAnt, catalogoVista[0].datos);

#if SERVIDOR
        ESPUI.print(modulHora, catalogoVista[0].datos);
#endif

#if DEBUG
// printf("Hora RTC:\t%s - %s\n", tiempo.timestamp(DateTime::TIMESTAMP_TIME), tiempo.timestamp(DateTime::TIMESTAMP_DATE));
#endif
      }
    }
    else
    {
      strcpy(catalogoVista[0].datos, "Error");
      strcpy(miHoraV, "___");
      tWebCabAnt = millis();
      cabInfo = String("Error RTC");

#if DEBUG
// Serial.println(F("No se puede leer el RTC."));
#endif
    }
    tRtcAnt = millis();
  }
#else
  strcpy(catalogoVista[0].datos, "XRTC");
  strcpy(miHoraV, "___");
  tWebCabAnt = millis();
  cabInfo = String("Error RTC");
#endif
}

String laHora(uint8_t x)
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    return "00";
  }

  char formattedTime[4];
  switch (x)
  {
  case 0:
    strftime(formattedTime, sizeof(formattedTime), "%H", &timeinfo);
    break;
  case 1:
    strftime(formattedTime, sizeof(formattedTime), "%M", &timeinfo);
    break;
  }
  return String(formattedTime);
}

void modoUdp()
{
  String miHora, h, m;
  if (true)
  {
    h = laHora(0);
    m = laHora(1);

    h = (h.length() < 2) ? "0" + h : h;
    m = (m.length() < 2) ? "0" + m : m;
    miHora = h + ":" + m;
    miHora.toCharArray(catalogoVista[0].datos, 6); // 00:00
  }
  else
  {
    strcpy(catalogoVista[0].datos, "Error");
    strcpy(miHoraV, "___");
    tWebCabAnt = millis();
    cabInfo = String("Error NTP");
  }

  int result = strcmp(catalogoVista[0].datos, miHoraAnt);
  if (result != 0)
  {
    miHora = h + "_" + m;
    miHora.toCharArray(miHoraV, 6); // 00_00
    strcpy(miHoraAnt, catalogoVista[0].datos);

#if SERVIDOR
    ESPUI.print(modulHora, catalogoVista[0].datos);
#endif

#if DEBUG
// printf("Hora UDP:\t%d:%d:%d - %d/%d/%d\n", hour(tiempoUnix), minute(tiempoUnix), second(tiempoUnix), day(tiempoUnix), month(tiempoUnix), year(tiempoUnix));
#endif
  }
}

void leerTempDHT()
{
  String temperature;
  // String hi;
  float t = dht.readTemperature() + (calibTemp);
  // float hic = dht.computeHeatIndex(t, dht.readHumidity(), false);
  if (isnan(t))
  {
    temperature = "00.00";
    // hi = "00.00";
    snprintf(catalogoVista[1].datos, 6, "00.0&");
    cabInfo = "Error de lectura temperatura.";
    tWebCabAnt = millis();
  }
  else
  {
    snprintf(catalogoVista[1].datos, 6, "%.1f&", t);
    temperature = String(t, 1) + "°C";
    // hi = String(hic, 1) + "°C";
  }

#if SERVIDOR
  ESPUI.print(modulTemp, temperature);
#endif
}

void leerHumDHT()
{
  float h = dht.readHumidity();
  if (isnan(h))
  {
    h = 00.0;
    cabInfo = "Error de lectura Humedad.";
    tWebCabAnt = millis();
  }
  snprintf(catalogoVista[2].datos, 6, "%.1f%s", h, "%");

#if SERVIDOR
  ESPUI.print(modulHum, catalogoVista[2].datos);
#endif
}

void cambiarEfecto()
{
  if (memAnimAle)
  {
    if ((millis() - tEfectoAnt) > (memAnimDuracion * 60000))
    {
      float temp_celsius = temperatureRead();

      // miEfecto = random(2, (sizeof(catalogo)/sizeof(catalogo[0])));
      miEfecto += miEfecto;
      miEfecto = miEfecto > 14 ? 2 : miEfecto;
      tEfectoAnt = millis();
    }
  }
  else
  {
    miEfecto = memAnimEfecto;
  }
}

int cambiarPantalla()
{
  if (modo == 4)
  {
    return 4;
  }

  if ((millis() - tModoAnt) > (catalogoVista[modo].tiempo * 1000))
  {
    do
    {
      modo = (modo + 1) % 3;
      if (modo == 0)
      {
        cambiarEfecto();
        rInicio = true;
        rFin = true;
        segundero = true;
      }
    } while (!catalogoVista[modo].estado);
    tModoAnt = millis();

    static long tAntAP = millis();
    if ((millis() - tAntAP) > 900000)
    {
      if ((WiFi.getMode() == WIFI_AP) && !memModAP)
      {
        ESP.restart();
      }
      tAntAP = millis();
    }
  }

  if (inicio)
  {
    miEfecto = random(2, (ARRAY_SIZE(catalogo)));
    inicio = false;
    return 3;
  }

  return modo;
}

void microfono()
{
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
        P.displayReset();
        P.displayText(" ", PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);

        tAntAplau = millis() + 1600;
        flanco = 0;
        tModoAnt = 0;
      }
    }
  }
}

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

int selectTemp(int est)
{
  if (!digitalRead(BTN_SW))
  {
    delay(400);
    est += 1;

    if (est == 2)
    {
      duracion = (mins * 60UL + secs) * 1000UL;
      tiempoInicio = millis();
    }
    else if (est == 3)
    {
      mins = 0;
      secs = 0;
      valor = 0;
    }
  }
  return est;
}

String obtenerTemp(int est)
{
  char buffer[7];
  bool sMin = (est == 99);
  int valor = sMin ? mins : secs;

  currentCLKState = digitalRead(A_CLK);
  if (currentCLKState != lastCLKState)
  {
    if (digitalRead(B_DT) != currentCLKState)
      valor++;
    else
      valor--;

    if (valor > est)
      valor = 0;
    else if (valor < 0)
      valor = est;
  }

  if (sMin)
    mins = valor;
  else
    secs = valor;

  if (segundero)
    sprintf(buffer, sMin ? "--:%02u" : "%02u:--", sMin ? secs : mins);
  else
    sprintf(buffer, "%02u:%02u", mins, secs);

  static unsigned long tAnt = 0;
  if ((millis() - tAnt) > 400)
  {
    segundero = !segundero;
    tAnt = millis();
  }

  lastCLKState = currentCLKState;
  return String(buffer);
}

String contaTempr()
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

// ========================================
void loop()
{
  static String dato;
  if ((millis() - tAntUpdate) > 3000)
  {
    leerTempDHT();
    leerHumDHT();
    memRelojUDP ? modoUdp() : modoRtc();
    tAntUpdate = millis();
  }
  (millis() < tAntUpdate) ? ESP.restart() : (void)0;

  if (memEncendido)
  {
    memAlarActiv ? sonarAlarma() : (void)0;
#if SERVIDOR
    cabInfo.length() > 0 ? webSiEstado() : (void)0;
#endif

    !sonando ? microfono() : (void)0;
    if (P.displayAnimate() && !sonando)
    {
      if (memMensaEst)
      {
#if SERVIDOR
        WebMostrarMsj();
#endif
      }
      else
      {
        cambiarEfecto();
        switch (cambiarPantalla())
        {
        case 0: // ====> RELOJ
          if (rInicio)
          { // inicio
            dato = String(catalogoVista[modo].datos);
            P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].velocidad, 600, catalogo[miEfecto].efecto, PA_NO_EFFECT);
            rInicio = false;
          }
          else
          {
            if (segundero)
            {
              dato = String(miHoraV);
              P.displayText(dato.c_str(), PA_CENTER, 0, 600, PA_NO_EFFECT, PA_NO_EFFECT);
            }
            else
            {
              dato = String(catalogoVista[0].datos);
              P.displayText(dato.c_str(), PA_CENTER, 0, 600, PA_NO_EFFECT, PA_NO_EFFECT);
            }
            segundero = !segundero;
          }
          break;

        case 1: // ====> TEMPERATURA
          if (rFin && catalogoVista[0].estado)
          { // fin
            if (segundero)
            {
              dato = String(miHoraV);
              P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].velocidad, 600, PA_NO_EFFECT, catalogo[miEfecto].efecto);
            }
            else
            {
              dato = String(catalogoVista[0].datos);
              P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].velocidad, 600, PA_NO_EFFECT, catalogo[miEfecto].efecto);
            }
            rFin = false;
          }
          else
          {
            dato = catalogoVista[modo].datos;
            P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].velocidad, catalogoVista[modo].tiempo * 1000, catalogo[miEfecto].efecto, catalogo[miEfecto].efecto);
          }
          break;

        case 2: // ====> HUMEDAD
          dato = catalogoVista[modo].datos;
          P.displayText(dato.c_str(), PA_CENTER, catalogo[miEfecto].velocidad, catalogoVista[modo].tiempo * 1000, catalogo[miEfecto].efecto, catalogo[miEfecto].efecto);
          break;

        case 3:
          dato = String(miIp);
          P.displayText(dato.c_str(), PA_CENTER, catalogo[3].velocidad + 15, 0, catalogo[3].efecto, catalogo[3].efecto);
          tModoAnt = millis();
          break;

        case 4: // ====> TEMPORIZADOR
          estadTemp = selectTemp(estadTemp);
          switch (estadTemp)
          {
          case 0:
            dato = obtenerTemp(99);
            P.displayText(dato.c_str(), PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
            break;

          case 1:
            dato = obtenerTemp(59);
            P.displayText(dato.c_str(), PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
            break;

          case 2:
            dato = contaTempr();
            P.displayText(dato.c_str(), PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
            break;

          case 3:
            digitalWrite(buzzer, LOW);
            modoTemp = false;
            modo = 0;
            break;
          }
          break;
        }
      }
    }
  }

  if (btnEncoder() && !modoTemp)
  {
    P.displayReset();
    P.displayText(" ", PA_CENTER, 0, 0, PA_NO_EFFECT, PA_NO_EFFECT);
    modoTemp = true;
    estadTemp = 0;
    modo = 4;
  }
}
