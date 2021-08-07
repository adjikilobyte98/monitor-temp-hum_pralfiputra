#include <util/atomic.h>
#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <DS3231.h>
#include <Wire.h>
#include <string.h>
//define pin dan tipe sensor
#define  LM35_pin  A0 
#define DHTPIN 4
#define DHTTYPE DHT11
//define address lokal dan tujuan
#define addrlokal 0xAA    //address node ini
#define addrtujuan 0x01   //address tujuan data yg akan dikirim
//--------------Jenis flag pesan, nilainya ditentukan sendiri, jenis data: byte--------------------
#define biasa 0x00        //|Nilainya ditentukan sendiri         |
#define master 0x01       //|karena library ini tidak            |
#define rts 0x02          //|memiliki konsep addressing,         |
#define cts 0x03          //|flag, dan semacamnya.               |
#define hnav 0x04         //|jadi, developer harus desain sendiri|
#define ack 0x05
//--------------  Address node  ------------------------------------------------------------------
#define addr1 0xAA
#define addr2 0xBB
#define addr3 0xCC
  byte normal = biasa;
  byte addrmaster = master;
  byte byterts = rts;
  byte bytects = cts;
  byte headernav = hnav;
  byte byteack = ack;
  byte node1 = addr1;
  byte node2 = addr2;
  byte node3 = addr3;
  int alamatini = 204; //alamat node ini, dalam byte yg diconvert ke int
  /*List alamat:
   * 0xAA = 170 = Node 1
   * 0xBB = 187 = Node 2
   * 0xCC = 204 = Node 3
   * Alamat perlu diubah ke bentuk integer karena masalah pada ESP8266/NodeMCU
   * yang tidak bisa menyimpan data byte. Setelah dicoba terus-menerus, telah
   * ditemukan masalah bahwa data byte berubah jadi integer, karena ESP8266/NodeMCU
   * (mungkin library-nya) menganggap data byte sebagai bilangan hexadecimal yang
   * kemudian terkonversi menjadi desimal integer.
   */
  int jeda = 25;  //jeda milidetik refresh LoRa dalam mode CAD (Carrier Activity Detection)


//-----------------------------   String experiment   ------------------------------------------------------------------------------
String datasuhu, datakelembaban, idpesan, pengirim, penerima, datanav;
char terminator = '|'; //digunakan untuk pemisah antar string, berguna untuk readStringUntil()
bool selesai; //flag untuk menandai bahwa proses rangkaian RTS/CTS nya selesai

//---------------   SETTING PIN LORA (Opsional)   --------------------------------------------------------------------------------
const int csPin = 7;          // LoRa radio chip select
const int resetPin = 6;       // LoRa radio reset
const int irqPin = 1;         // change for your board; must be a hardware interrupt pin
//--------------PARAMETER PESAN-----------------------------
byte msgCount = 0;      // nomor pesan (iterasi ke-)
byte localAddress = addrlokal;   // address perangkat ini
byte destination = addrtujuan;   // address tujuan pengiriman
long lastSendTime = 0;  // waktu pengiriman terakhir
int interval = 2000;   // jeda antar pengiriman selanjutnya (diambil dari pengambilan data DHT11
unsigned int slottime = 31; //(ms) Slot time adalah waktu yang data perlukan untuk sampai ke tujuan 
                              //diambil dari Time on Air pada LoRa Calculator
unsigned int difs; //jeda DIFS diambil dari dua kalinya slottime

//------------Interrupt Service Routine (ISR)---------------
// Apa yang dilakukan LoRa ketika ada sebuah interrupt. ISR harus sesederhana mungkin
static volatile bool interruptHappened = false;
static void isr_pinD3() {
    interruptHappened = true;
}
//---------Variabel backoff time dan penanda CAD------------
int backoff;
bool caddetected;
unsigned long mikro, milli;
unsigned long iterasi;
unsigned long tstamp;
unsigned int nretries = 3; //nilai faktor, dimulai dari 3
unsigned int bin, K, binbackoff; //membuat variabel bin (eksponen biner), K (jumlah slot time dari eksponen biner),
                              //dan bin backoff (hasil perkalian slot time dengan slot ke-K)

//============== Variabel kalibrasi sensor ======================
float kallm35 = 0; //kalibrasi suhu LM35
float kalsuhu = 0;  //kalibrasi nilai suhu
float kallembab = 0; //kalibrasi nilai kelembaban

DS3231  rtc(7, 8); //SDA, SCL
DHT dht(DHTPIN, DHTTYPE);
float k, s;
float templm35;

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  rtc.begin();
  dht.begin();
  analogReference(INTERNAL); //Uno memiliki Vref 1.1V, digunakan untuk proses convert nilai analaog LM35 ke digital
  
//  LoRa.setSignalBandwidth(125E3); //defaultnya 125E3 (125000 Hz)
//  LoRa.setSpreadingFactor(7); //defaultnya 7
//  LoRa.setCodingRate4(5); //defaultnya 5
//  LoRa.setTxPower(17); //defaultnya 17
  LoRa.begin(923E6);
  while (!Serial);

  Serial.println("LoRa Sender - Node 3");

  if (!LoRa.begin(923E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

  // For other constants, like FHSS change channel, CRC error, or RX timeout, see the LoRa.h header file.
  // Choose from LORA_IRQ_DIOx_ variants and use this "x" number in place of the first parameter.
  // Not all DIOx and interrupt type mixes are possible.
  LoRa.setInterruptMode(0 /* DIO0 */, LORA_IRQ_DIO0_CADDONE);

  // Launch our ISR function when LoRa interrupt happens
  attachInterrupt(digitalPinToInterrupt(2), isr_pinD3, RISING);

  // Start LoRa CAD process
  LoRa.cad();

  Serial.println("Starting LoRa succeeded");
  
}

void loop() {

  mikro = micros();
  milli = millis();

  k = dht.readHumidity() + kallembab;
  s = dht.readTemperature() + kalsuhu;
  float bk;
  float bs;
  if(isnan(k) || isnan(s)){
    k = 0;
    s = 0;
    float k = dht.readHumidity();
    float s = dht.readTemperature();
    };

  templm35 = (analogRead(LM35_pin) / 9.3) + kallm35; // Read analog voltage and convert it to °C ( 9.3 = 1023/(1.1*100) )
//  sprintf(text, "%3u.%1u%cC", (int)templm35, (int)(templm35*10)%10, 223); //debugging

  
  if (interruptHappened) {
        interruptHappened = false;

        const uint8_t loraInterrupts = LoRa.readInterrupts();
        if (loraInterrupts & LORA_IRQ_FLAG_CAD_DETECTED) { // Here use LORA_IRQ_FLAG_* variants from LoRa.h
//            LoRa.parsePacket(); // Put into RXSINGLE mode
            // ... Process packet if there's one
            Serial.println("\n\n=========Interupt!=========\n\n");
            caddetected = 1;
            
            if (nretries > 10){
              nretries = 5;
            }
            
              
            nretries++;
            bin = pow(2, nretries) + 0.5; //function pow outputnya float, untuk menampilkan outputnya sebagai integer, bulatkan ke atas dengan menambah nilai 0.5
            K = random(31, bin)-1; //pilih slot time random dari eksponen biner
            
            onReceive(LoRa.parsePacket());
            // kalau ada aktivitas transmisi data atau selesai melakukan transmisi data dengan RTS/CTS, node nunggu dulu (backoff)
            backoff = K*slottime; //nomor slot waktu dikalikan dengan slot time
                                               
            Serial.print("Jeda backoff (ms): ");
            Serial.println(backoff);
            LoRa.idle();
            delay(backoff);
        }

        // Do not let the ISR function and the loop() write and read the interruptHappened variable at the same time
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            // It's possible that the ISR function had set interruptHappened to "true" while we were receiving packets.
            // Check again to be sure that we clear interrupt flags only when we received no further IRQs.
            if (!interruptHappened) {
                LoRa.clearInterrupts(LORA_IRQ_FLAG_CAD_DETECTED | LORA_IRQ_FLAG_CAD_DONE);
//                Serial.println("...nothing..."); //for debugging
                caddetected = 0;
                
//                if (millis() - lastSendTime > interval && caddetected == 0) {
//        onReceive(LoRa.parsePacket());
        programlora();
//        sendMessageString();    //ngirim data, tapi berbentuk string
        lastSendTime = millis();            // timestamp the message
        interval = 2000;    //2d diambil dari waktu sensing DHT11
                                     
        Serial.print("Interval (ms): ");    // Setiap transmisi data ada jeda interval untuk memberi
        Serial.println(interval);          // kesempatan node lain untuk melakukan transmisi
//        LoRa.idle();
//        delay(interval); //
//                  }
                }
            }
        }
        //--------LoRa masuk ke mode Idle lalu melanjutkan ke mode CAD (Carrier Activity Detection) untuk cek traffic data-------------
        LoRa.idle();
        delay(jeda); //25ms
        LoRa.cad();
        delay(jeda); //25ms
        //total = 50ms. Tiap 50ms, node cek kondisi channel
        
}


//===============================================================================================
//                 Program Bagian LoRa
//===============================================================================================
void programlora(){
    iterasi++;
    Serial.println(" \n\n");
    Serial.print("===================== NODE 3 Iterasi ke-");
    Serial.println((String)iterasi + "=====================");
  Serial.println(templm35);
  Serial.print(s);
  Serial.print(" C - ");
  Serial.print(k);
  Serial.println("% RH");

//  Serial.print(rtc.getTimeStr());
  Serial.print("-----------------------");
    
//    sendMessage();
//    onReceive(LoRa.parsePacket());
    sendingrts();
//    Serial.println("Data Terkirim...");
    lastSendTime = millis();            // timestamp the message
    interval = 2000;    // 2-6 seconds
    nretries = 5;
}


//===========================  Kode Utama LoRa  ========================================================
/*    Bagian ini berisi mengenai proses pembacaan data yang ada pada sebuah channel, mekanisme penanganan
 * data jika ada data balasan dari master node berupa CTS, ACK, atau data berisi alokasi waktu untuk sebuah
 * node lainnya untuk transmisi data/NAV (Network Allocation Vector), dan mekanisme penanganan data jika
 * data berasal dari node lain.
 */
//======================================================================================================
void onReceive(int ukuranPaket) {
  
  int npengirim, npenerima, nidpesan, ndatanav; //buat variabel untuk wadah konversi string ke integer
  if (ukuranPaket == 0) return;          // if there's no packet, return

  tstamp = micros();
  Serial.println("-------*** DATA MASUK ***-------");
  // read packet header bytes:
  penerima = LoRa.readStringUntil(terminator);          // recipient address
  pengirim = LoRa.readStringUntil(terminator);            // sender address
  idpesan = LoRa.readStringUntil(terminator);     // incoming msg ID
  datanav = LoRa.readStringUntil(terminator);    //data NAV

  npenerima = penerima.toInt();
  npengirim = pengirim.toInt();
  nidpesan = idpesan.toInt();
  ndatanav = datanav.toInt();

  
  Serial.print("Data dari: ");
  Serial.print(npengirim);
  Serial.print("  |  Untuk: ");
  Serial.println(npenerima);
  Serial.println("ID Pesan: (2 = RTS, 3 = CTS, 4 = NAV, 5 = ACK)");
  Serial.println(nidpesan);
  Serial.print("Data NAV: ");
  Serial.println(ndatanav);

  if (npengirim == 1){   //Jika data datang dari master node
    if (npenerima != alamatini) {   //jika pesan tidak ditujukan ke node ini
      Serial.println("Paket CTS bukan untuk node ini\n");
      Serial.print("Delay NAV (ms): ");
      Serial.println(ndatanav);
      LoRa.idle();
      delay(ndatanav);
      return;   //kalau selesai, skip
      }

    else if (npenerima == alamatini){

        if (nidpesan == 3){ //0x03 = 3 = Jenis pesan CTS
        tstamp = micros();
//        Serial.println("CTS Diterima... | Timestamp CTS (micros): " + (String)micros());
        Serial.println("CTS Diterima... | Waktu CTS diterima: " + (String)tstamp);
//        delay(100); //DIFS nya 100ms (interval waktu antara node nerima rts/cts dengan transmisi data
//        Serial.println("Mengirim data... | Timestamp (micros): " + (String)micros());
        sendMessage();
        
      } //akhir kondisi-if nidpesan == 3

      if (nidpesan == 5){ //0x05 = 5 = Jenis pesan ACK
//        unsigned long timeack = micros();
//          Serial.println("ACK diterima... | Timestamp ACK (micros): " + (String)micros());
//        Serial.println("ACK Diterima... | Waktu ACK diterima: " + (String)timeack);
//        delay(100); //DIFS nya 100ms (interval waktu antara node nerima rts/cts dengan transmisi data
//        Serial.println("--------TRANSMISI SELESAI-------\n");
        ACK();
        return; //selesai, langsung kembali ke program utama
       
      } //akhir kondisi-if nidpesan == 5
      
  
    } //akhir kondifi-if npenerima == alamatini
  } //akhir kondisi-if npengirim == 1
  
  else if (npengirim != 1){
    // kalau ada aktivitas transmisi data, berarti channel tersebut ada traffic
    //node harus nunggu dulu (backoff)
    K = random(31, bin)-1;
      backoff = K*slottime; //2dtk s.d. 6dtk. 2 dtk diambil dari waktu delay sampling DHT11, 6 dtk diambil
                                          //dari total delay sampling DHT11 semua 3 node ( 2x3 = 6)
      Serial.print("Jeda backoff (ms): ");
      Serial.println(backoff);
      LoRa.idle();
      delay(backoff);
      return;
    }

}// akhir function ini


//===================  FUNCTION PENGIRIMAN DATA SUHU & KELEMBABAN  =======================================
/*    Bagian ini berisi tentang proses pengiriman data sensing suhu dan kelembaban menuju master node.
 * Data yang ditransmisikan berisi alamat tujuan, alamat node, jenis/ID pesan, data suhu & kelembaban
 * bertipe data string (Data diubah ke bentuk string untuk memudahkan pengiriman data, data akan diubah
 * kembali ke float oleh master node. Selain itu, ESP8266/NodeMCU kesulitan membaca data numerik,
 * khususnya data bertipe byte, jadi harus dikirim ke bentuk string dahulu untuk kemudian dikonversi lagi
 * ke data numerik).
 */
//========================================================================================================
void sendMessage() {
  Serial.println("Mengirim data... | Timestamp (micros): " + (String)micros());
//  unsigned long timesend = micros();
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100); //setiap tahap transmisi data, ada jeda SIFS 100ms
  LoRa.beginPacket();                   // memulai pengiriman
  LoRa.write(destination);              // menambahkan byte alamat tujuan
  LoRa.write(node3);             // menambahkan byte alamat lokal (perangkat ini)
  LoRa.write(biasa);                 // menambahkan ID/Nomor pesan
  LoRa.println((String)templm35 + "|");
  LoRa.println((String)k + "|");
  LoRa.endPacket();                     // akhir dari paket data, dan kirimkan
  tstamp = micros();
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("Data terkirim... | Waktu Data dikirim (micros): " + (String)tstamp);
  
}



//=======================================  Function RTS  ======================================================
/*    Bagian ini berisi tentang pengiriman paket RTS menuju master node. Data yang ditransmisikan ke master node
 * meliputi alamat tujuan, alamat pengirim, dan jenis/id pesan.
 *    Setelah paket RTS dikirmkan, node menunggu paket CTS selama 2 detik untuk memastikan master node siap menerima
 * data dari node ini. Jika setelah 2 detik node belum menerima balasan CTS, maka node kembali ke mode CAD, yaitu
 * mode di mana modul LoRa kembali menyimak traffic data pada sebuah channel gelombang radio.
 */
//=============================================================================================================
void sendingrts(){
  Serial.println("\n*************** NODE 3 || pengiriman pesan RTS ke master...");
//  Serial.println("Proses pengiriman pesan RTS ke master...");
//  unsigned long timerts = micros();
  delay(100); //setiap proses transmisi ada jeda SIFS 100ms
  digitalWrite(LED_BUILTIN, HIGH);
  LoRa.beginPacket();
  LoRa.write(destination);  //kirim ke...
  LoRa.write(node3);        //Pengirim
  LoRa.write(byterts);      //ID Pesan (0x02 = 2 = RTS)
  LoRa.endPacket();
  tstamp = micros();
  Serial.println("RTS Terkirim... | Timestamp (micros): " + (String)tstamp);
  digitalWrite(LED_BUILTIN, LOW);
//  Serial.println("Waktu RTS: " + (String)timerts);
  for (int i = 0; i <= 300; i++){ //Node nunggu 6dtk untuk menantikan pesan CTS. 300 = 6000/20 (delay-nya 20ms)
    onReceive(LoRa.parsePacket());
    handleRTSCTS();
    delay(20);
  }
}

//=====================================  Function keputusan CAD  ============================================
void handleRTSCTS(){

  if (selesai == 1){
    Serial.println("Proses transmisi data dengan metode CSMA/CA");
    Serial.println("beserta mekanisme RTS/CTS telah selesai!");
    Serial.println("Kembali ke mode CAD...\n");
    selesai = 0;
    return;
  }
}


void ACK(){

//    for (int i = 0; i <= 100; i++){  //setelah node ngirim data, node menunggu pesan ACK selama 2s dtk untuk memastikan data terkirim, 100 = 2000/20 (delay-nya 10s)
//      penerima = LoRa.readStringUntil(terminator);// recipient address
//      pengirim = LoRa.readStringUntil(terminator);// sender address
//      idpesan = LoRa.readStringUntil(terminator);// incoming msg ID
//
//      int npenerima = penerima.toInt();
//      int npengirim = pengirim.toInt();
//      int nidpesan = idpesan.toInt();

//    if (npengirim == 1){
//      if (npenerima == alamatini){
//      if (nidpesan == 5){  //0x05 = 5 = ACK
        tstamp = micros();
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("ACK diterima! Data sudah sampai di tujuan | Timestamp (micros): " + (String)tstamp);
        selesai = 1;
        digitalWrite(LED_BUILTIN, LOW);
        return;   //kalau udah ACK, OK langsung di-skip function-nya
//        }
//      else {
//        Serial.println("   Data tidak tersampaikan...   ");
//        return; //akhiri function
//          }
//        }
//      delay(20);
//        } //akhir looping-for nunggu ACK
//    }
}
