/*****************************************************************//**
 * \file   main.cpp
 * \brief 
 *
 * \author Afrina Rahaman
 * \date  04 May 2023
 * OPGAVE:
 * Varme styring til et hus eller lejlighed. 
 * Fra en webside på en arduino eller en mobil kunne du sætte en ønsket temperatur i et eller flere rum. 
 * På siden kunne man angive en komfort temperatur  (normal temperatur) og så en spare temperatur. 
 * Skiftet mellem spare og komfort sker så ud fra et tidsskema. 
 * F.eks kl. 06:30 tænder varmen og opvarmer så huset til 
 * komfort temperatur. Kl 8:30 sænkes temperaturen så igen til spare temperatur.
***********************************************************************/

/**
 * KODER FOR OPGAVEN
 
*/

/**
*Forskellige Bibliotekter*
*/
#ifdef ESP32
  #include <WiFi.h>
  #include <AsyncTCP.h>
  #include <time.h>
#else
  #include <Arduino.h>
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>



/** 
 * NETWORK CREDENTIALS
 */
const char* ssid = "E308";
const char* password = "98806829";
/**
* definerende variabel for at får nuværende tid fra NTPServer
*/ 
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
int current_time;

/**
 * Definerende variabel for nuværende temperature, 
 * day/night temperature, komfort temperature
 */ 

String inputMessage = "25.0";
String inputParam;
int startTimeInput;
int endTimeInput;
String lastTemperature;
String comfort_temp = "false";
String inputMessage2 = "23.0";
float day_night_temp;
float comfort_temperature;

/**
 * MAIN KODE
*/

/**
 * HTML web page to handle 2 input fields (day_night_input, comfort_input) 
 * og viser chart to romm temperature
 
 */
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>Temperature Threshold Output Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://code.highcharts.com/highcharts.js"></script>
  </head><body>
  <h2> Temperature with sensor</h2> 
  <h3>%TEMPERATURE% &deg;C</h3>
   <div id="chart-temperature" class="container"></div>
  <form action="/get">
    Temperature in day/night  <input type="number" step="0.1" name="day_night_input" required><br>
   
    
   
    Start Time for day/night temperature  <input type="number" step="0.1" name="start_time_input" required><br>
   
    
    
    End Time for day/night <input type="number" step="0.1" name="end_time_input" required><br>
   
    <input type="submit" value="Day/Night">
    </form>
   <form action="/get">
    Temperature in comfort  <input type="number" step="0.1" name="comfort_input"  required><br>
   
    <input type="submit" value="Comfort">
  </form>
</body>
<script>
var chartT = new Highcharts.Chart({
  chart:{ renderTo : 'chart-temperature' },
  title: { text: 'Current Temperature' },
  series: [{
    showInLegend: false,
    data: []
  }],
  plotOptions: {
    line: { animation: false,
      dataLabels: { enabled: true }
    },
    series: { color: '#059e8a' }
  },
  xAxis: { type: 'datetime',
    dateTimeLabelFormats: { second: '%H:%M:%S' }
  },
  yAxis: {
    title: { text: 'Temperature (Celsius)' }
    
  },
  credits: { enabled: false }
});
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var x = (new Date()).getTime(),
          y = parseFloat(this.responseText);
      //console.log(this.responseText);
      if(chartT.series[0].data.length > 40) {
        chartT.series[0].addPoint([x, y], true, true, true);
      } else {
        chartT.series[0].addPoint([x, y], true, false, true);
      }
    }
  };
  xhttp.open("GET", "/lastTemp", true);
  xhttp.send();
}, 30000 ) ;
</script>
</html>)rawliteral";

/**
 * Hvis hjemmeside får ikke, det giver 404 error
*/
void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

/**
 * hjemmeside viser i port 80
*/
AsyncWebServer server(80);

/**
 * Erstatter pladsholder med DS18B20-værdier
  */
String processor(const String& var){
  //Serial.println(var);
  if(var == "TEMPERATURE"){
    return lastTemperature;
  }
  else if(var == "DAYNIGHT"){
    return inputMessage;
  }
  else if(var == "COMFORT"){
    return comfort_temp;
  }
  return String();
}

/**
 * Flag variabel for at holde styr på, om triggere var aktiveret eller ej
*/
// Flag variable to keep track if triggers was activated or not
bool triggerActive = false;

const char* PARAM_INPUT_1 = "day_night_input";
const char* PARAM_INPUT_2 = "comfort_input";
const char* PARAM_INPUT_3 = "start_time_input";
const char* PARAM_INPUT_4 = "end_time_input";

/**
 * Interval mellem sensoraflæsninger
*/
// Interval between sensor readings
unsigned long previousMillis = 0;     
const long interval = 5000;  

/**
 * GPIO, hvor udgangen er tilsluttet
*/
// GPIO where the output is connected to
const int output_daynight = 26;
const int output_comfort = 25;

/**
 * GPIO, hvor DS18B20 er tilsluttet
*/

// GPIO where the DS18B20 is connected to
const int oneWireBus = 27;     

/**
 * Konfigurer en oneWire-instans til at kommunikere med alle OneWire-enheder
*/
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);

/**
 * Send vores oneWire reference til Dallas temperatursensor
*/
// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);


/**
 * sætter nuværende tid i 24hrs format i en variabel(current_time)
*/
void setTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
 
  current_time= timeinfo.tm_hour;
  
   
}


void setup() {
  Serial.begin(115200);
  /**
   * Forbinder med Wi-Fi
  */
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed!");
    return;
  }
  Serial.println();
  /**
   * får IP addresse af ESP32
  */
  Serial.print("ESP IP Address: http://");
  Serial.println(WiFi.localIP());
  
/**
 * Init og få tiden
*/
 
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  setTime();

/**
 * definere pinmode som LAV
*/

  pinMode(output_daynight, OUTPUT);
  digitalWrite(output_daynight, LOW);
  pinMode(output_comfort, OUTPUT);
  digitalWrite(output_comfort, LOW);
  
  /**
   * Start DS18B20-sensoren
  */
 
  sensors.begin();
  
  /**
   * Send webside til klient
  */

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/lastTemp", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.print("XX");
    request->send_P(200, "text/plain", lastTemperature.c_str());
  });

  /**
   * Modtag en HTTP GET-anmodning på <ESP_IP>/get?day_night_input=<inputMessage>&comfort_input=<inputMessage2>
  */
  
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    /**
     * GET day_night_input værdi på <ESP_IP>/get?day_night_input=<inputMessage>
    */
   
    if (request->hasParam(PARAM_INPUT_1)) {
      inputMessage = request->getParam(PARAM_INPUT_1)->value();
      day_night_temp= (request->getParam(PARAM_INPUT_1)->value()).toFloat();

      inputParam = PARAM_INPUT_1;
      startTimeInput=(request->getParam(PARAM_INPUT_3)->value()).toInt();
      endTimeInput=(request->getParam(PARAM_INPUT_4)->value()).toInt();
    }
    /**
     * GET comfort_input værdi på <ESP_IP>/get?comfort_input=<inputMessage2>
    */

    else if (request->hasParam(PARAM_INPUT_2)) {
        inputMessage = request->getParam(PARAM_INPUT_2)->value();
        inputParam = PARAM_INPUT_2;
        comfort_temperature=(request->getParam(PARAM_INPUT_2)->value()).toInt();
        
    }
    else {
        inputMessage2 = "23.0";
        comfort_temp="true";
    }
    
    Serial.println(inputMessage);
    Serial.println(comfort_temp);
    request->send(200, "text/html", "HTTP GET request sent to your ESP on input field (" 
                                     + inputParam + ") with value: " + inputMessage +
                                     "<br><a href=\"/\">Return to Home Page</a>");
  });
  server.onNotFound(notFound);
  server.begin();
}

void loop() {
  
  unsigned long currentMillis = millis();
  /**
   * erklære en variabel for at gemme den ønskede temperatur
  */
   
  float desiredTemp;

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    sensors.requestTemperatures();

  /**
  * Kald sensors.requestTemperatures() for at udstede en global temperatur 
  * og anmodninger til alle enheder på bussen
  */
    // Temperature in Celsius degrees 
    float temperature = sensors.getTempCByIndex(0);
    Serial.print("Current room temperature: ");
    Serial.print(temperature);
    Serial.println(" *C");
    
    /**
     * får last temperaturenv fra sensor
    */
    lastTemperature = String(temperature);

/**
 * kontrollerer, om inputtiden er lig med eller større end den nuværende tid og gemmer det i et variabel
*/
    // Determine which mode to use based on current time and schedule
  bool is_room_temp_day_night = (current_time >=startTimeInput  && 
                             current_time < endTimeInput);
 
 /**
  * Tjek om temperaturen er day_night eller ej
 */
    // Check if temperature is day_night or not
    if(inputMessage && inputParam == PARAM_INPUT_1 && is_room_temp_day_night   ){
      //String message = String("Day_night temperature is being set. which is: ") + 
                          String(temperature) + String("C");
     
      
     /**
      * Hvis klient giver input day_night tempemperature med starttime og endtime, 
      * så tænder Rød LED og slukker Gul LED   
     */
      desiredTemp=day_night_temp;
      Serial.println("Desired temp for day_night with starttime and endtime");
      Serial.print(desiredTemp);
       Serial.println(" *C");
      Serial.println(startTimeInput);
       Serial.println(endTimeInput);
        digitalWrite(output_daynight, HIGH);
      digitalWrite(output_comfort, LOW);
      
      
    }
    /**
      * Hvis klient giver input til comfort temperature, 
      * så tænder Gul LED og slukker Rød LED  
     */
    // Check if temperature is comfort or not
    else if(inputMessage && inputParam == PARAM_INPUT_2 )
    {
        comfort_temp="true";
        desiredTemp=comfort_temperature;
        Serial.println("Desired temp for comfort tempearture");
      Serial.println(desiredTemp);
        Serial.print(" *C");
      digitalWrite(output_comfort, HIGH);
      digitalWrite(output_daynight, LOW);
    }
   // else digitalWrite(output_comfort, HIGH);
    /**
      * Hvis klient giver input day_night tempemperature med forkert starttime og endtime, 
      * så spørger om at sætte rigtig tid   
     */
   else if(inputMessage && inputParam == PARAM_INPUT_1 && !is_room_temp_day_night )
   {
   
      desiredTemp=inputMessage.toFloat();
      Serial.println("Desired temp for comfort tempearture");
      Serial.println(desiredTemp);
        Serial.print(" *C");
       Serial.println("set time properly");
   }
       
    
  }
}
