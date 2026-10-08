#include "TcpNetwork.h"
#include "Secrets.h"
#include "DccExProtocol.h"

#include <WiFi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// ============================================================
// CONFIGURATION TCP
// ============================================================

static constexpr uint16_t TCP_PORT = 2560;


// ============================================================
// SERVEUR / CLIENT
// ============================================================

static WiFiServer tcpServer(TCP_PORT);

static WiFiClient tcpClient;


// ============================================================
// ETAT RESEAU
// ============================================================

static volatile bool wifiConnected = false;
static volatile bool clientConnected = false;


// ============================================================
// STATISTIQUES
// ============================================================

static volatile uint32_t clientConnectionCount = 0;
static volatile uint32_t clientDisconnectionCount = 0;
static volatile uint32_t tcpByteCount = 0;


// ============================================================
// CONNEXION WIFI
// ============================================================

static void connectWifi() {

  Serial.println();
  Serial.println("Connexion Wi-Fi...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD);


  while (WiFi.status() != WL_CONNECTED) {

    Serial.print(".");

    delay(500);
  }


  wifiConnected = true;


  Serial.println();
  Serial.println();

  Serial.println(
    "Wi-Fi connecte");


  Serial.print(
    "SSID       : ");

  Serial.println(
    WiFi.SSID());


  Serial.print(
    "Adresse IP : ");

  Serial.println(
    WiFi.localIP());


  Serial.print(
    "RSSI       : ");

  Serial.print(
    WiFi.RSSI());

  Serial.println(
    " dBm");


  Serial.println();
}


// ============================================================
// NETWORK TASK
// ============================================================

static void NetworkTask(
  void *parameter) {

  for (;;) {

    // --------------------------------------------------------
    // SURVEILLANCE WIFI
    // --------------------------------------------------------

    if (WiFi.status() != WL_CONNECTED) {

      if (wifiConnected) {

        wifiConnected = false;

        Serial.println(
          "Wi-Fi deconnecte");
      }


      vTaskDelay(
        pdMS_TO_TICKS(1000));

      continue;
    }


    wifiConnected = true;


    // --------------------------------------------------------
    // NOUVEAU CLIENT TCP
    // --------------------------------------------------------

    if (!tcpClient ||
        !tcpClient.connected()) {

      if (clientConnected) {

        clientConnected = false;

        clientDisconnectionCount++;

        Serial.println(
          "Client TCP deconnecte");
      }


      WiFiClient newClient =
        tcpServer.accept();


      if (newClient) {

        tcpClient =
          newClient;

        clientConnected =
          true;

        clientConnectionCount++;


        Serial.print(
          "Client TCP connecte : ");

        Serial.println(
          tcpClient.remoteIP());
      }
    }


    // --------------------------------------------------------
    // DONNEES RECUES
    // --------------------------------------------------------

    if (tcpClient &&
        tcpClient.connected()) {

      while (tcpClient.available()) {

        int value =
          tcpClient.read();

        if (value >= 0) {

          tcpByteCount++;


          // --------------------------------------------------
          // PROTOCOLE DCC-EX
          //
          // Le transport TCP transmet simplement chaque
          // octet au parseur.
          //
          // Le parseur DCC-EX est independant de la couche
          // reseau.
          // --------------------------------------------------

          processDccExByte(
            static_cast<uint8_t>(value));
        }
      }
    }


    // --------------------------------------------------------
    // TACHE BASSE PRIORITE
    // --------------------------------------------------------

    vTaskDelay(
      pdMS_TO_TICKS(10));
  }
}


// ============================================================
// INITIALISATION RESEAU
// ============================================================

void initNetwork() {

  // ----------------------------------------------------------
  // PROTOCOLE DCC-EX
  // ----------------------------------------------------------

  initDccExProtocol();


  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  connectWifi();


  // ----------------------------------------------------------
  // SERVEUR TCP
  // ----------------------------------------------------------

  tcpServer.begin();

  tcpServer.setNoDelay(
    true);


  Serial.print(
    "Serveur TCP : port ");

  Serial.println(
    TCP_PORT);


  // ----------------------------------------------------------
  // NETWORK TASK
  // ----------------------------------------------------------

  BaseType_t result;

  result =
    xTaskCreate(
      NetworkTask,
      "NetworkTask",
      4096,
      nullptr,
      1,
      nullptr);


  if (result != pdPASS) {

    Serial.println(
      "ERREUR creation NetworkTask");

    while (true) {
      delay(1000);
    }
  }
}


// ============================================================
// STATUS RESEAU
// ============================================================

void printNetworkStatus() {

  Serial.println(
    "--------------------------------------");

  Serial.println(
    " RESEAU");

  Serial.println(
    "--------------------------------------");


  Serial.printf(
    "Wi-Fi            : %s\n",
    wifiConnected
      ? "connecte"
      : "deconnecte");


  if (WiFi.status() == WL_CONNECTED) {

    Serial.printf(
      "SSID             : %s\n",
      WiFi.SSID().c_str());


    Serial.print(
      "Adresse IP       : ");

    Serial.println(
      WiFi.localIP());


    Serial.printf(
      "RSSI             : %d dBm\n",
      WiFi.RSSI());
  }


  Serial.printf(
    "Port TCP         : %u\n",
    TCP_PORT);


  Serial.printf(
    "Client TCP       : %s\n",
    clientConnected
      ? "connecte"
      : "deconnecte");


  Serial.printf(
    "Connexions TCP   : %lu\n",
    (unsigned long)clientConnectionCount);


  Serial.printf(
    "Deconnexions TCP : %lu\n",
    (unsigned long)clientDisconnectionCount);


  Serial.printf(
    "Octets TCP recus : %lu\n",
    (unsigned long)tcpByteCount);


  // ----------------------------------------------------------
  // STATUS DCC-EX
  // ----------------------------------------------------------

  printDccExStatus();
}