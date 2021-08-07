#include "CTBot.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <LoRa.h>
#include <NTPClient.h>
#include <Wire.h>
#include <RTClib.h>
//#include <Arduino.h>
 
#define ss 15
#define rst 16
#define dio0 2
//--------------Jenis flag pesan, nilainya ditentukan sendiri, jenis data: byte--------------------
/*Library NodeMCU nggak bisa langsung declare nilai berupa byte,
jadi nilai byte harus diconvert ke nilai desimal*/
//#define biasa 0x00
//#define addrlokal 0x01
//#define rts 0x02
//#define cts 0x03
//#define hnav 0x04
////--------------  Address node  ------------------------------------------------------------------
//#define addr1 0xAA//170 //
//#define addr2 0xBB//187 //
//#define addr3 0xCC//204 //
  byte normal = 0x00;
  byte addrmaster = 0x01;
  byte byterts = 0x02;
  byte bytects = 0x03;
  byte hnav = 0x04;
  byte byteack = 0x05;
  byte node1 = 0xAA;
  byte node2 = 0xBB;
  byte node3 = 0xCC;
  int ack = 5; //0x05 = 5
//---------------- Flags --------------------------------------------------------
  int counterpesan;
  bool flagnotif;
  unsigned long mikro, milli;



RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jum'at", "Sabtu"};
CTBot myBot;
CTBotReplyKeyboard myKbd;
bool isKeyboardActive;
const long utcOffsetInSeconds = 25200;
String ssid = "nama_wifi";
String pass = "pass";
String token = "token_telegram";
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "id.pool.ntp.org", utcOffsetInSeconds);
WiFiClientSecure client;
DateTime now = rtc.now();
uint32_t timestamp = now.unixtime();

//------------------  Variabel untuk menyimpan data sensing dari tiap node  -----------
float suhu1, suhu2, suhu3;
float lembab1, lembab2, lembab3;


////------------  Pemisah data String  --------------------------------------------------
char terminator = '|';
//======================  Kalibrasi ===============================================
float kalsuhu1 = -1.2, kalsuhu2 = -1.9, kalsuhu3 = -2.4;
float kallembab1 = 9.0, kallembab2 = 9.0, kallembab3 = 7.0;

//--------------PARAMETER PESAN--------------------------------------------------------
byte msgCount = 0;      // nomor pesan (iterasi ke-)
byte localAddress = 0x01;   // address perangkat ini
//byte destination = addrtujuan;   // address tujuan pengiriman
long lastSendTime = 0;  // waktu pengiriman terakhir
int interval = 2000;   // jeda antar pengiriman selanjutnya (diambil dari pengambilan data DHT11

//---------Variabel backoff time dan penanda CAD---------------------------------------
int backoff;
bool caddetected;

//---------------  Variabel wadah pesan Telegram & timer  ------------------------------
String pesan;
  unsigned long currentMillis;
  unsigned long prevMillis;
  unsigned long txIntervalMillis = 10000;
  unsigned long intervalnotif = 0; 
  unsigned long waktupesanterakhir = 0;
  unsigned long 
//----------------  Variabel waktu  ----------------------------------------------------
int detik, menit, jam, hari, bulan, tahun;



void setup() {
  Serial.begin(115200);
  while (!Serial);
  Wire.begin(0, 2); //SDA D3, SCL D4
  
  Serial.println("LoRa Receiver");
  LoRa.setPins(ss, rst, dio0);
//  LoRa.setSpreadingFactor(8);
//  LoRa.setCodingRate4(8);
  if (!LoRa.begin(923E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  Serial.println("LoRa OK");

  Serial.println("Starting TelegramBot...");
  myBot.wifiConnect(ssid, pass);
  myBot.setTelegramToken(token);
  if (myBot.testConnection())
    Serial.println("\ntestConnection OK");
  else
    Serial.println("\ntestConnection NOK");

  myKbd.addButton("Tes Tombol");
  myKbd.addButton("Status Suhu");
  myKbd.addButton("Status Kelembaban");
  myKbd.addRow();
  myKbd.addButton("Help");
  myKbd.addRow();
  myKbd.addButton("Matikan Tombol");
  myKbd.enableResize();
  isKeyboardActive = false;

  timeClient.begin();
    timeClient.update();
    rtc.begin();
}
 
void loop() {

//  nav.navtimer = random(6000); //diambil dari delay sensing DHT11
  mikro = micros();
  milli = millis();

  timeClient.update();
  DateTime now = rtc.now();
  uint32_t timestamp = now.unixtime();
  
  onReceive(LoRa.parsePacket());
//  receiveData(LoRa.parsePacket());    //nerima data, tapi berbentuk string

  currentMillis = millis();
    
  Serial.print("* "); //just for debugging
//  ---------------  refresh telegram  ----------------------
  if (currentMillis - prevMillis >= txIntervalMillis) {
        Serial.println("************************************");
//        send();
        telegram();
        
        Serial.println();
        prevMillis = millis();
    }

  
  delay(10);
}







//===============================TELEGRAM===================================//
void telegram(){
    //variabel untuk nyimpan data pesan telegram
  TBMessage msg;
  DateTime now = rtc.now();
  uint32_t timestamp = now.unixtime();

  //cek jika ada pesan dari user...
  if (myBot.getNewMessage(msg)) {
    Serial.println("Pesan baru di Telegram API...");
    if (msg.messageType == CTBotMessageText) {
      if (msg.text.equalsIgnoreCase("tombol")) {
        myBot.sendMessage(msg.sender.id, "Menu tombol diaktifkan.\nAnda dapat mengirim contoh teks, cek status suhu, status kelembaban, dan lain-lain.", myKbd);
        isKeyboardActive = true;
      }
     else if (msg.text.equalsIgnoreCase("help")) {
        myBot.sendMessage(msg.sender.id, (String)"'status suhu': Untuk mengetahui keadaan suhu.\n" + 
        (String)"\n'status kelembaban': Untuk mengetahui keadaan kelembaban.\n" + 
        (String)"\nJika tombol tidak bekerja karena node mati lampu atau baru saja diprogram,\n" + 
        (String)"ketik 'tombol' lagi untuk inisialisasi ulang.");
      }
     else if (msg.text.equalsIgnoreCase("Status Suhu")) {
      myBot.sendMessage(msg.sender.id, "Status suhu pada gudang penyimpanan:\n" +
        (String)"\nWaktu: " + (String)timeClient.getDate() + (String)"-" + (String)timeClient.getMonth() + (String)"-" + (String)timeClient.getYear() + 
      (String)" / " + (String)timeClient.getHours() + (String)":" + (String)timeClient.getMinutes() + (String)":" + (String)timeClient.getSeconds() +
        "\nLantai 1: " + (String)suhu1 +
        "°C\nLantai 2: " + (String)suhu2 +
        "°C\nLantai 3: " + (String)suhu3 + "°C");
    }
    else if (msg.text.equalsIgnoreCase("Status Kelembaban")) {
      myBot.sendMessage(msg.sender.id, "Status suhu pada gudang penyimpanan:\n" +
        (String)"\nWaktu: " + (String)timeClient.getDate() + (String)"-" + (String)timeClient.getMonth() + (String)"-" + (String)timeClient.getYear() + 
      (String)" / " + (String)timeClient.getHours() + (String)":" + (String)timeClient.getMinutes() + (String)":" + (String)timeClient.getSeconds() +
        "\nLantai 1: " + (String)lembab1 +
        " %\nLantai 2: " + (String)lembab2 +
        " %\nLantai 3: " + (String)lembab3 + " %");
    }

    else if (msg.text.equalsIgnoreCase("help")) {
        myBot.sendMessage(msg.sender.id, (String)"'status suhu': Untuk mengetahui keadaan suhu.\n" + 
        (String)"\n'status kelembaban': Untuk mengetahui keadaan kelembaban.\n" + 
        (String)"\nJika tombol tidak bekerja karena node mati lampu atau baru saja diprogram,\n" + 
        (String)"ketik 'tombol' lagi untuk inisialisasi ulang.");
      }
    


    
      //jika menu tombol aktif
      else if (isKeyboardActive) {
        if (msg.text.equalsIgnoreCase("Matikan Tombol")) {
          myBot.removeReplyKeyboard(msg.sender.id, "Menu tombol dinonaktifkan");
          isKeyboardActive = false;
        } 

      else if (msg.text.equalsIgnoreCase("Status Suhu")) {
      myBot.sendMessage(msg.sender.id, "Status suhu pada gudang penyimpanan:\n" +
        (String)"\nWaktu: " + (String)timeClient.getDate() + (String)"-" + (String)timeClient.getMonth() + (String)"-" + (String)timeClient.getYear() + 
      (String)" / " + (String)timeClient.getHours() + (String)":" + (String)timeClient.getMinutes() + (String)":" + (String)timeClient.getSeconds() +
        "\nLantai 1: " + (String)suhu1 +
        "°C\nLantai 2: " + (String)suhu2 +
        "°C\nLantai 3: " + (String)suhu3 + "°C");
    }
    else if (msg.text.equalsIgnoreCase("Status Kelembaban")) {
      myBot.sendMessage(msg.sender.id, "Status suhu pada gudang penyimpanan:\n" +
        (String)"\nWaktu: " + (String)timeClient.getDate() + (String)"-" + (String)timeClient.getMonth() + (String)"-" + (String)timeClient.getYear() + 
      (String)" / " + (String)timeClient.getHours() + (String)":" + (String)timeClient.getMinutes() + (String)":" + (String)timeClient.getSeconds() +
        "\nLantai 1: " + (String)lembab1 +
        " %\nLantai 2: " + (String)lembab2 +
        " %\nLantai 3: " + (String)lembab3 + " %");
    }

      else if (msg.text.equalsIgnoreCase("help")) {
        myBot.sendMessage(msg.sender.id, (String)"'status suhu': Untuk mengetahui keadaan suhu.\n" + 
        (String)"\n'status kelembaban': Untuk mengetahui keadaan kelembaban.\n" + 
        (String)"\nJika tombol tidak bekerja karena node mati lampu atau baru saja diprogram,\n" + 
        (String)"ketik 'tombol' lagi untuk inisialisasi ulang.");
      }

        
        else {
          myBot.sendMessage(msg.sender.id, msg.text);
        }
      } else {
        myBot.sendMessage(msg.sender.id, "Coba ketik 'tombol'\nAtau ketik 'help' untuk bantuan");
      }
    }
  }
  Serial.println("Tugas Telegram API selesai...");
}
//====================================****=======================================//
//            NOTIFIKASI OVERHEAT / OVERHUMID
//====================================****=======================================//

void notifoverheat(){
  TBMessage notif;
  myBot.sendMessage(id_telegram_tujuan, (String)"Yth Pengelola Gudang Penyimpanan PR. ALFI PUTRA TRENGGALEK, pemberitahuan bahwa suhu/kelembaban gudang telah melampaui batas omptimal," + 
        (String)"\ndengan rincian sebagai berikut: " +
        "\nLantai 1: " + (String)suhu1 +
        "°C\nLantai 2: " + (String)suhu2 +
        "°C\nLantai 3: " + (String)suhu3 +
    "°C" + (String)"\n\nKelembaban: " +
    "\nLantai 1: " + (String)lembab1 +
        " %\nLantai 2: " + (String)lembab2 +
        " %\nLantai 3: " + (String)lembab3 + " %");
  Serial.println("\n\npesan notifikasi dikirimkan...\n");
}








//=============================  LORA  ====================================//

//------------------  Baca transmisi data  ----------------------------------

//=============  String transmit expereiment  ==================
//Tabel byte, nilai byte dibuat sendiri, karena library ini tidak ada konsep
//addressing dan penetapan header/jenis pesan.
/*0x00 = 0 = Pesan biasa
 * 0x01 = 1 = alamat master node
 * 0x02 = 2 = Pesan RTS
 * 0x03 = 3 = Pesan CTS
 * 0x04 = 4 = Pesan NAV
 * 0xAA = 170 = alamat node 1
 * 0xBB = 187 = alamat node 2
 * 0xCC = 204 = alamat node 3
 */

void onReceive(int ukuranPaket) {

  if (ukuranPaket == 0) return;          // if there's no packet, return

  //------------  String experiment  ----------------------
byte datasuhu, datakelembaban, idpesan, pengirim, penerima, datanav;
int npengirim, npenerima, nidpesan, ndatanav, ndatasuhu, ndatakelembaban;
String ssuhu, slembab;

 //membuat wadah konversi string ke float. pastikan isi variabel kosong
 //terlebih dahulu.
float fdatasuhu = 0, fdatakelembaban = 0;

  Serial.print("\n\n-------------Data Masuk dari: ");
  penerima = LoRa.read();          // recipient address
  pengirim = LoRa.read();            // sender address
  idpesan = LoRa.read();     // incoming msg ID
  ssuhu = LoRa.readStringUntil(terminator); //baca data hingga sebuah pemisah ditemukan
  slembab = LoRa.readStringUntil(terminator);

  //konversi string ke float
  fdatasuhu = ssuhu.toFloat();
  fdatakelembaban = slembab.toFloat();


//  //debugging
//  Serial.println(" ");
//  Serial.println("debug: ");
//  Serial.print((String)penerima + " | ");
//  Serial.print((String)pengirim + " | ");
//  Serial.println(idpesan);
//  Serial.println(ssuhu);
//  Serial.println(slembab);
//  Serial.println("Khusus float: ");
//  Serial.println(fdatasuhu);
//  Serial.println(fdatakelembaban);
//  Serial.println("-----------------------------------");
//  Serial.println(" ");

  
  Serial.print(pengirim);
  Serial.print(" | Untuk: ");
  Serial.println(penerima);
  Serial.println("ID Pesan: (0, = Biasa, 2 = RTS, 3 = CTS, 4 = NAV");
  Serial.println(idpesan);
  Serial.print("Data suhu: ");
  Serial.println(fdatasuhu);
  Serial.print("Data kelembaban: ");
  Serial.println(fdatakelembaban);



  //=========  seleksi pesan  ========

  //pengecekan jenis data
  if (idpesan == 2){   //jika master nerima paket rts, master balas dengan cts
  ndatanav = 2000; //2-6 detik, 2 detik diambil dari sampling DHT11,
                                //6 detik diambil dari keseluruhan delay sampling DHT11
                                //dari tiap node (2x3 = 6)
  Serial.print("Ada RTS dari: ");
  Serial.println(pengirim);
//  delay(100); //setiap tahap transmisi data, ada jeda SIFS 100ms
  LoRa.beginPacket();
  LoRa.print((String)pengirim + terminator); //tujuan paket cts, dikirim ke alamat si pengirim rts
  LoRa.print((String)penerima + terminator); //memasukkan alamat dirinya (master)
  LoRa.print((String)bytects + terminator); //ID pesan ada di bagian ini ya (bag. ke-3)
  LoRa.print((String)ndatanav + terminator); //data berisi NAV (Network Allocation Vector)
  LoRa.endPacket();
  Serial.println("CTS & NAV Terkirim...");
  return;
  }
  
  else if (idpesan == 0){ //0 = 0x00 = normal, 2 = 0x02 = RTS, 3 = 0x03 = CTS
   
    Serial.println("Data Baru!");

    if (pengirim == 170){ //170 = 0xAA = Alamat Node 1
    //jika data dari node 1, data yg diterima tadi ditaruh di wadah variabel 1
    suhu1 = fdatasuhu + kalsuhu1;
    lembab1 = fdatakelembaban + kallembab1;
    Serial.println("Data dari node 1");
    }
    else if (pengirim == 187){ //187 = 0xBB = Alamat Node 2
    //jika data dari node 2, data yg diterima tadi ditaruh di wadah variabel 2
    suhu2 = fdatasuhu + kalsuhu2;
    lembab2 = fdatakelembaban + kallembab2;
    Serial.println("Data dari node 2");
    }
    else if (pengirim == 204){ //204 = 0xCC = Alamat Node 3
    //jika data dari node 3, data yg diterima tadi ditaruh di wadah variabel 3
    suhu3 = fdatasuhu + kalsuhu3;
    lembab3 = fdatakelembaban  + kallembab3;
    Serial.println("Data dari node 3");
    }

    //Setelah data diterima, master node kirim pesan ACK ke node pengirim tadi
//    delay(100); //setiap tahap transmisi data, ada jeda SIFS 100ms
    LoRa.beginPacket();
    LoRa.print((String)pengirim + terminator);
    LoRa.print((String)penerima + terminator);
    LoRa.print((String)byteack + terminator);
    LoRa.endPacket();
    Serial.println("ACK dikirim...\n");

    Serial.print("Suhu Node 1: ");
    Serial.print(suhu1);
    Serial.print(" | Lembab Node 1: ");
    Serial.println(lembab1);
    Serial.print("Suhu Node 2: ");
    Serial.print(suhu2);
    Serial.print(" | Lembab Node 2: ");
    Serial.println(lembab2);
    Serial.print("Suhu Node 3: ");
    Serial.print(suhu3);
    Serial.print(" | Lembab Node 3: ");
    Serial.println(lembab3);

    cekkondisi();
  }

  
}







void cekkondisi(){
  if ((suhu1 != 0) && (suhu2 != 0) && (suhu3 != 0)){ //<=== Untuk memastikan semua data masuk dulu
  if ((suhu1<29 || suhu1>31) || (suhu2<29 || suhu2>31) || (suhu3<29 || suhu3>31)){
    if (millis() - waktupesanterakhir > intervalnotif) {
    notifoverheat();
    waktupesanterakhir = millis();
    intervalnotif = 3600000; //satuan dalam ms, notif overheat dikirim tiap 1 jam (3600x1000 = 3600000)
  }
 }
}
  else{
    intervalnotif = 0; //reset limit interval per jam
  }
  
}

































//void cekkondisi(){
//  if (millis() - waktupesanterakhir > intervalnotif) {
//  if (suhu1>31 || suhu2>31 || suhu3>31 || suhu1<29 || suhu2<29 || suhu3<29 && counterpesan == 0){
//    counterpesan++;
//    notifoverheat();
//    waktupesanterakhir = millis();
//  }
//  else if (suhu1>31 || suhu2>31 || suhu3>31 || suhu1<29 || suhu2<29 || suhu3<29 && counterpesan >= 1 && counterpesan < 1000000){
//    counterpesan ++;
//    waktupesanterakhir = millis();
//  }
//
//  else if (suhu1>31 || suhu2>31 || suhu3>31 || suhu1<29 || suhu2<29 || suhu3<29 && counterpesan == 1000000){
//    counterpesan = 1;
//    notifoverheat();
//    waktupesanterakhir = millis();
//  }
//  else {
//    counterpesan = 0;
//    waktupesanterakhir = millis();
//  }
//}
//}
