/*******************************************

  Hecho por:
  
 -Marcelo Hernandez 222272
 -Diego Marvid 225080
 -Valentin Otte 222308

  #InstaWatt 2018

*********************************************/


// incluye de bibliotecas para comunicación
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>




//  configuración datos wifi
#define WIFI_AP "WIFIORT"
#define WIFI_PASSWORD "PASSWORDORT"




//  configuración datos thingsboard
#define NODE_NAME "PRRUEBA"
#define NODE_TOKEN "PRUEBA"

// incluye de bibliotecas para el sensor de temperatura y humedad
#include <OneWire.h>
#include <DHTesp.h>
#include <DallasTemperature.h>

//Sensor temperatura exterior
DHTesp dht;

// incluye de bibliotecas para el sensor de temperatura (termocupla)
#define ONE_WIRE_BUS D1
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);



char thingsboardServer[] = "demo.thingsboard.io";

/*definir topicos.
 * telemetry - para enviar datos de los sensores
 * request - para recibir una solicitud y enviar datos 
 * attributes - para recibir comandos en baes a atributtos shared definidos en el dispositivo
 */
char telemetryTopic[] = "v1/devices/me/telemetry";
char requestTopic[] = "v1/devices/me/rpc/request/+";  //RPC - El Servidor usa este topico para enviar rquests, cliente response
char attributesTopic[] = "v1/devices/me/attributes";  // Permite recibir o enviar mensajes dependindo de atributos compartidos


// declarar cliente Wifi y PubSus
WiFiClient wifiClient;
PubSubClient client(wifiClient);


// configuración sensores
// declarar variables control loop (para no usar delay() en loop
unsigned long lastSend;
const int elapsedTime = 10; // tiempo transcurrido entre envios al servidor
float tiempoMedida = 10;// tiempo transcurrido entre medidas.
const int muestras = 100;//Cantidad de muestras de corriente tomadas por tiempo de medida



//Variable en donde guardamos la temperatura agua.
float celsius = 0;




int i = 0;
//-----------------Variables para el procesado del consumo electrico---------------------//
int lastMeasures[muestras];//Array en donde guardmos el voltaje del sensor de corriente.
double voltajeSensorj = 0;//voltaje dado por el sensor de corriente.
double conversion = 1;//Factor de conversion entre el voltaje del sensor y la corriente.
double corrientej;//Corriente calculada.
double voltajej = 0;//Voltaje calculado.
double acum = 0.0;//variable usada pra guardar los datos en la integracion numerica.
boolean calefonPrendido = false;
//---------------------------------------------------------------------------------------//


//-----------------------------------Caudalimetro---------------------------------------//
volatile int flow_frequency; // mide la cantidad de pulsos dado por el sensor de caudal.
unsigned int l_hour; // Variable la cual guarda el consumo de agua en litros por hora.
//--------------------------------------------------------------------------------------//



//--------------------------Variables de consumo de agua--------------------------------//
double cantidadAgua = 0; // Variable que suma: (caudal/3600s)=Cantidad de agua que gasto
double energiaConsumida = 0;
//--------------------------------------------------------------------------------------//

//SETUP:
void setup()
{
  Serial.begin(9600);
  delay(10);

  // inicializar wifi y pubsus
  connetToWiFi();
  client.setServer( thingsboardServer, 1883 );

  // agregado para recibir callbacks
  client.setCallback(on_message);//El metodo que llama para el callback es on_message.
   
  lastSend = 0;//variable para controlar cada cuanto tiempo se envian datos.
  
  pinMode(ONE_WIRE_BUS, INPUT_PULLUP);
  //El pin D0 es el pin del relay
  pinMode(D0, OUTPUT);
  sensors.begin();
  
  //el pin D2 es el pin del sensor de temperatura y humedad.
  dht.setup(4); //GPIO4 -> D2

  //el pin D4 es el pin del caudalimetro.
  pinMode(D5, INPUT);
  digitalWrite(D5, HIGH); //Pull-up opcional.
  //para medir el caudal, se necesita hacer una interrupcion asociada al pin del caudalimetro.
  attachInterrupt(14, flow, RISING); //GPIO14-> D5, flow es el metodo que llama al interrumpir y rising es el estado del input en el cual interrumpe.
  sei(); // Habilita la interrupcion.
  
}


//Metodos de devolucion para el json a mandar de cada medida:

//Metodo para la temperatura exterior:
String leerTempExterior() {
  //Obtenemos los datos del sensor:
  float humedad = dht.getHumidity();
  float tempExterior = dht.getTemperature();
  
  //Pasamos los datos del sensor a string.
  String humedadStr = String(humedad);
  String tempExteriorStr = String(tempExterior);
  
//Creamos el string ret, el cual es una parte del Json con los datos obtenidos.
  String ret = "";
  ret = "\"TempExterior\":"; ret += tempExteriorStr; ret += ",";
  ret += "\"Humedad\":"; ret += humedadStr; ret += ",";

  return ret;
}


//Metodo para la temperatura del agua:
String leerTempAgua() {
   //Obtenemos los datos del sensor:
  sensors.requestTemperatures();
  
  //De los datos solo tomamos la temperatura en C°.
  celsius = sensors.getTempCByIndex(0);

  //Pasamos los datos del sensor a string.
  String celsiusStr = String(celsius);

  //Creamos el string ret, el cual es una parte del Json con los datos obtenidos.
  String ret = "";
  ret = "\"TemperaturaAgua\":"; ret += celsiusStr;
  
  return ret;
}


//Metodo para la potencia por hora:
String promedioCorriente() {

  acum = 0.0;
  corrientej;
  voltajeSensorj = 0;
  voltajej = 0;

//Recorro el array de medidas del sensor.
  for (int j = 0; j < muestras; j++) {
    //Ajusto el voltaje del sensor, restamos 512 pues montamos a onda a un voltaje de 1.65V
    voltajeSensorj = lastMeasures[j] - 512; //470
    //Calculamos la miedida del sensor en voltaje.
    voltajej = 3.3 * voltajeSensorj / 1023;
    //Calculamos la corriente real la cual pasa por el calefon.
    corrientej = conversion * voltajej * 10;
    //Agregamos la medida de corriente a la variable acum, la suma los cuadrados de la corriente por el dt. 
    acum = acum + corrientej * corrientej * tiempoMedida / 1000;
  }
  //Calculamos en IRMS el valor de la corriente.
  double Irms = sqrt(acum / (muestras * tiempoMedida / 1000));
  
  //Calculamos la potencia consumida 
  float potencia = 0; 

  if(calefonPrendido){
    potencia = 230*Irms;
  }
  
  //Pasamos los datos obtenidos a string.
  String potenciaStr = String(potencia);
  
  //Creamos el string ret, el cual es una parte del Json con los datos obtenidos.
  String ret = "\"Potencia\":"; ret += potenciaStr; ret += ",";

//en paralelo, en una variable aparte guardamos el consumo energetico del dispositivo, dado que la energia es Pot/t, este metodo calcula potencia por hora y es llamado cada un segundo la energia queda de tal forma:
  energiaConsumida += ((double)potencia/3000.0);

  return ret;
}



void flow () // Interrupciones del caudalimetro
{
//Siempre que se llama a la interrupcion a la variable se le suma 1
   flow_frequency++;
}


//Metodo para el caudal y consumo de agua por hora:
String leerCaudal(){
  
  l_hour = (flow_frequency * 60 / 7.5); // (frecuencia de pulso x 60 min) / 7.5Q = caudal en litros por hora.
  //reseteamos el contador de frecuencia de pulsos.
  flow_frequency = 0;


  //Pasamos los datos obtenidos a string.
  String caudalStr=String(l_hour);
  
  //Creamos el string ret, el cual es una parte del Json con los datos obtenidos.
  String ret = "\"Caudal\":"; ret += caudalStr; ret += ",";

//en paralelo, en una variable aparte guardamos el consumo de agua del dispositivo, dado que el mismo es Q/t, este metodo calcula caudal en litros por hora y es llamado cada un segundo el agua consumida:
  cantidadAgua += ((double)l_hour/3600.0);
  
  return ret;
}



//Creacion de jsons para enviar mensaje:

String hacerJsonTelemetria() {
  
  String json = "{";
  json += leerCaudal();
  json += promedioCorriente();
  json += leerTempExterior();
  json += leerTempAgua();
  json += "}";  
  
return json;

}



// función loop micro
void loop()
{
  if ( !client.connected() ) {
    reconnect();
  }

  if ( millis() - lastSend > elapsedTime ) { // Cada un segundo envia datos y se actualiza.
   
     //Siempre entra y realiza una medida del sensor de corriente.
     lastMeasures[i] = analogRead(A0);
     i++;
     
     
    //Cuando se carga el arrar (1 segundo)
    if (i >= muestras) {
      
    //Reicicia el contador de posicione sdel array.
      i = 0;
      
    // Enviar datos de telemetria
    getAndSendData();
    }
    
    lastSend = millis();
    
  }

  client.loop();
}




//Lectura de sensores y enviado de telemetria y atributos
void getAndSendData()
{
 // Serial.println("Tomando datos.");
  
  String payloadTelemetria = hacerJsonTelemetria();

  // Envio Json de telemetria con datos de sensores hacia el topico de telemetria
  char atributosTelemetria[100];
  payloadTelemetria.toCharArray( atributosTelemetria, 100 );
  if (client.publish( telemetryTopic, atributosTelemetria ) == true){
    Serial.println("Telemetria publicada con exito");
  

  }



}

/* 
 *  Este callback se llaman cuando se utilizan widgets de control que envian mensajes por el topico requestTopic
 *  en la función de reconnect se realiza la suscripción al topico de request
 */
void on_message(const char* topic, byte* payload, unsigned int length) 
{
  char json[length + 1];
  strncpy (json, (char*)payload, length);
  json[length] = '\0';
  StaticJsonBuffer<200> jsonBuffer;
JsonObject& data = jsonBuffer.parseObject((char*)json);


  //Decodificamos el JSON:
  if (!data.success())
  {
    Serial.println("parseObject() failed");
    return;
  }

  // Obtener el nombre del método invocado, esto lo envia el switch de la puerta y el knob del motor que están en el dashboard
  String methodName = String((const char*)data["method"]);


  //responder segun el método 
  if (methodName.equals("switchPrendido")) {
    replyBotonRequest((const char*)data["params"],topic);
  }

  else if (methodName.equals("setTempAgua")) { 
    replyTempElegidaRequest(data["params"],topic);
  }

  else if (methodName.equals("setModoInteligente")) {
    replyModoInteligente((const char*)data["params"],topic);
  }
  else if (methodName.equals("setModoComfort")) { 
    replyModoComfort((const char*)data["params"],topic);
  }
  
  else if (methodName.equals("controlarCalefon")) {
     boolean estado_calefon = data["params"];
     switchCalefon(estado_calefon);
   
  }
  else if (methodName.equals("consumoRequest")) {
    replyEnergiaYAguaConsumida(topic);
  
  }
   else if (methodName.equals("resetConsumoRequest")) {
    //Reiniciamos el consumo de agua y energia cada 24hr.
    cantidadAgua = 0;
    energiaConsumida = 0;
  }


 
}


/*
 * función que "abre" la puerta (simulada)
 */
void switchCalefon(boolean action)
{
 
if (action == true) {
//Prende el calefon.
    digitalWrite(D0,LOW);
    calefonPrendido = true;
  }
   else {
//Apaga el calefon.
    digitalWrite(D0,HIGH); 
    calefonPrendido = false;
   }
  
}

//Replies al servidor:

//Reply al boton del modo inteligente:
 void replyModoInteligente(String estadoModoInteligente, const char* topic)
{

    // cambiar el topico de RPC a RESPONSE
    String responseTopic = String(topic);
    responseTopic.replace("request", "response");  //Notar que se cambio la palabra request por response en la cadena del topico
    
     // Prepara un JSON payload string con el modo inteligente:
     String payload = "{";
     payload += "\"ModoInteligente\":"; payload += "\""  ; payload += estadoModoInteligente; payload += "\"";
     payload += "}";

    //Envia el payload en un array de char.
    char attributes[100];
    payload.toCharArray( attributes, 100 );


    // se envia la repsuesta la cual se utiliza para modificar variables usadas en las rules.
    client.publish(attributesTopic, attributes);
}


//Reply al boton del modo comfort:
 void replyModoComfort(String estadoModoComfort, const char* topic)
{
String mensajeTemp=" ";
if(estadoModoComfort=="true"){
  mensajeTemp="Seleccion de temperatura inhabilitada (MCA)";
 
}else{
  mensajeTemp="Seleccione la temperatura deseada";

}

    // cambiar el topico de RPC a RESPONSE
    String responseTopic = String(topic);
    responseTopic.replace("request", "response");  //Notar que se cambio la palabra request por response en la cadena del topico
    
 
     // Prepara un JSON payload string con el modo comfort:
     String payload = "{";
     payload += "\"ModoComfort\":"; payload += "\""  ; payload += estadoModoComfort; payload += "\"";
     payload += ",";
     payload += "\"TempHabilitada\":"; payload += "\""  ; payload +=mensajeTemp; payload += "\"";
     payload += "}";
Serial.println(mensajeTemp);

    //Envia el payload en un array de char.
    char attributes[100];
    payload.toCharArray( attributes, 100 );

     // se envia la repsuesta la cual se utiliza para modificar variables usadas en las rules. 
    client.publish(attributesTopic, attributes);
}


//Reply el cual envia los datos de energia y agua consumida:
void replyEnergiaYAguaConsumida(const char* topic)
{

    // cambiar el topico de RPC a RESPONSE
    String responseTopic = String(topic);
    responseTopic.replace("request", "response");

    //Pasamos a string la cantidad de agua consumida.
     String cantAguaStr= String(cantidadAgua);
     
     // Prepara un JSON payload string con la cantidad de agua y energia consumida.
     String payload = "{";
     payload += "\"CantidadAguaGastada\":"; payload += "\""  ; payload += cantAguaStr; payload += "\"";
     payload += ",";
     payload += "\"EnergiaConsumida\":"; payload += "\""  ; payload += energiaConsumida; payload += "\"";
     payload += "}";


    //Envia el payload en un array de char.
    char attributes[100];
    payload.toCharArray( attributes, 100 );

    // se envia la repsuesta la cual se utiliza para modificar variables las cuales se muestran en tarjetas.
    client.publish(attributesTopic, attributes);
}


//Reply el cual envia los el estado actual del relay:
void replyBotonRequest(String estadoRele, const char* topic)
{

    // cambiar el topico de RPC a RESPONSE
    String responseTopic = String(topic);
    responseTopic.replace("request", "response");
    
     // Prepara un JSON payload string con el estado actual del relay, esta informacion es utilizada en las rules.
     String payload = "{";
     payload += "\"estadoBoton\":"; payload += "\""  ; payload += estadoRele; payload += "\"";
     payload += "}";

  
    //Envia el payload en un array de char.
    char attributes[100];
    payload.toCharArray( attributes, 100 );

     // se envia la repsuesta la cual se utiliza para modificar variables usadas en las rules. 
    client.publish(attributesTopic, attributes);
}


//Reply el cual envia la temperatura seleccionada en el widget:
void replyTempElegidaRequest(double valorTemperatura, const char* topic){
    // cambiar el topico de RPC a RESPONSE
    String responseTopic = String(topic);
    responseTopic.replace("request", "response");  //Notar que se cambio la palabra request por response en la cadena del topico
    
  // Prepara un JSON payload string con la temperatura deseada del agua, esta informacion es utilizada en las rules.
     String payload = "{";
     payload += "\"tempElegida\":"; payload += "\""  ; payload += valorTemperatura; payload += "\"";
     payload += "}";
     

    //Envia el payload en un array de char.
    char attributes[100];
    payload.toCharArray( attributes, 100 );

 
     // se envia la repsuesta la cual se utiliza para modificar variables usadas en las rules. 
    client.publish(attributesTopic, attributes);  
}


/*
 * funcion para reconectarse al servidor de thingsboard y suscribirse a los topicos de RPC y Atributos
 */
void reconnect() {
  int statusWifi = WL_IDLE_STATUS;
  // Loop until we're reconnected
  while (!client.connected()) {
    statusWifi = WiFi.status();
    connetToWiFi();
    
    Serial.print("Connecting to ThingsBoard node ...");
    // Attempt to connect (clientId, username, password)
    if ( client.connect(NODE_NAME, NODE_TOKEN, NULL) ) {
      Serial.println( "[DONE]" );
      
      // Subscribing to receive RPC requests 
      client.subscribe(requestTopic); 
      
    } else {
      Serial.print( "[FAILED] [ rc = " );
      Serial.print( client.state() );
      Serial.println( " : retrying in 5 seconds]" );
      // Wait 5 seconds before retrying
      delay( 5000 );
    }
  }
}

/*
 * función para conectarse a wifi
 */
void connetToWiFi()
{
  Serial.println("Connecting to WiFi ...");
  // attempt to connect to WiFi network

  WiFi.begin(WIFI_AP, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
}



