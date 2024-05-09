  /* This is code for a rudamentry smartlock. Run an an Arduino R4 with Wifi, it includes multiple functionalities including :
  - Locking & Unlocking a Lock (Represented by a Servo) using a keypad.
  - Relevant LED's to show lock status.
  - A doorbell that plays a chime when rung.
  - A doorbell that also sends a notification to your phone when rung.
  - A web interface that (once tunnelled via ngrok) can be accessed from anywhere via : https://zmdesign.work 
  This web interface allows the user to lock/unlock the door and, if necessary, change the passcode.
  - A streamed webcam that is embedded within the website. This displays a live feed from the 'doorbell'. 

  The below code has been informed from a number of various codebases and existing programs. Some of these include

    Basic Wifi Functionality has been adapted from Tom Igoe's LED Webserver Sketch
    https://docs.arduino.cc/tutorials/uno-r4-wifi/wifi-examples#simple-webserver

    ESP32 Web Functionality with Ngrok has been adapted from Patrick McDowell and Rui Santons
    https://ngrok.com/blog-post/putting-the-esp32-microcontroller-on-the-internet 
    https://randomnerdtutorials.com/esp32-web-server-arduino-ide/ 

    SD card integration has been adapted from David A. Mellis, Tom Igoe and Marcus Schappi
    https://docs.arduino.cc/learn/programming/sd-guide/ 
    https://learn.littlebirdelectronics.com.au/arduino/sd-card-module-with-arduino

    Use of PROGMEM has been adapted from https://www.arduino.cc/reference/en/language/variables/utilities/progmem/ 

    Touch Capacitor Functionality adapted from RDIoT Demo, Mikael Abehsera, Aliumas, ITimeWaste and various other forums.
    This was notably quite tricky, ChatGPT and AI Tools helped me implement this functionality, also.
    https://www.youtube.com/watch?v=Wq3hgxJ3U6U 
    https://rdiot.tistory.com/134
    https://forum.arduino.cc/t/ttp229-16-channel-digital-capacitive-switch-touch/242100 
    http://itimewaste.blogspot.com/2014/12/arduino-code-for-ttp229-touch-16-button.html 

    8 Segment LED Display adapted from Bas on Tech,
    https://www.youtube.com/watch?v=DaMDhQauAXI 
    https://arduino-tutorials.net/tutorial/control-max7218-8-digit-led-display-with-arduino 

    LED Display Control Library : https://www.arduino.cc/reference/en/libraries/ledcontrol/ 
    Cloudflare Webcam Streaming Functionality : https://developers.cloudflare.com/stream/ 
    MyNotifier Functionality : https://mynotifier.stoplight.io/docs/mynotifier/78784d80b519b-send-notification 

  This project has been inspired by a number of physical computing practicitioners, including :
  The 'fish doorbell' as outlined in my previous assignment. https://visdeurbel.nl/en/the-fish-doorbell/ 
  Mark Rober's Glitter Bomb project https://www.youtube.com/watch?v=xoxhDk-hwuo&vl=en - showcasing the ability of incorperating cameras and a web interface into a physical computing context.
  Alex Davies' Dislocation - http://schizophonia.com/portfolio/ - Showcasing the use of livestreaming within an immersive media environment
  I did a things 'Home Security System' - https://www.youtube.com/watch?v=o8sj7uxLg88&pp=ygUSaSBkaWQgYSB0aGluZyB3aWZp - Again showcasing the use of security via a 'doorbell' / camera feed
  
  Though this project does have some flaws (security vulnerabilities, the webcam being streamed off a different device) I think it is a good, rudamentary attempt at a smart lock. If I were to continue with this project, I would look to first scale it up (using an actual digital lock / door rather than a servo) and password protect the website. I would also look at upgrading the chipset to run the webcam locally.
  Finally, I believe it has fantastic potential to integrate numerous other features, including custom ringtones, multi-digit passcodes, prank effects and integration with other smart home technologies (if the doorbell is rung, the porch light is turned on.)
  */ 

  #include <Servo.h> // Include the servo library and declare a servo object.
  Servo servo; 

  #include "LedControl.h" // Include the LedControl library. We can specify some parameters -  pin 7 on DIN, pin 6 on Clk, pin 5 on LOAD, number of displays: 1. This is managed through the included library in the folder
  LedControl lc = LedControl(7, 6, 5, 1);

  #include "WiFiS3.h" // Include the WiFi Library to serve our web server / notification requests
  #include <SPI.h> // Include the SPI Library, SD card library and a File class
  #include <SD.h> 
  File webFile;

  #define CLOCK_PIN 2 // Define some variables for use with the Touch Capacitor
  #define DATA_PIN 3

  int status = WL_IDLE_STATUS; // Create a wifi variable status
  WiFiServer server(80); // Open a server on port 80

  // Initialise a Buffer and specify a buffer size. This will later hold web page content so the SD card is not always being read from.
  const int BUFFER_SIZE = 5024;
  char webPageBuffer[BUFFER_SIZE]; 

  // Set our initial variables for use throughout the program. These arre self explanatory.
  bool lockUnlocked = false; // Variable to track lock status.

  // LED Pins
  const int unlockedLedPin = A1; 
  const int lockedLedPin = A2;   

  // Network Login Information 
  char ssid[] = "";  // Network Name
  char pass[] = "";  // Network Password

  // Speaker
  int piezoPin = 8;

  // Doorbell
  int buttonPin = 9;
  int prevButtonState = HIGH;

  // Initial Passcode
  String passcode = "1234"; 

  void setup() {
    Serial.begin(9600);
    
    // Attaching our various output / inputs to their respective pins
    servo.attach(4); 
    pinMode(buttonPin, INPUT_PULLUP);
    pinMode(CLOCK_PIN, OUTPUT);
    pinMode(DATA_PIN, INPUT);
    digitalWrite(CLOCK_PIN, HIGH);
    pinMode(piezoPin, OUTPUT);
    pinMode(unlockedLedPin, OUTPUT);
    pinMode(lockedLedPin, OUTPUT);

    // Turn both LEDs are off initially
    digitalWrite(unlockedLedPin, LOW);
    digitalWrite(lockedLedPin, LOW);

    // Wake up our number display with some initial parameters
    lc.shutdown(0, false);
    lc.setIntensity(0, 5);
    lc.clearDisplay(0);

    // SD Card Setup
    // Check if the SD can be reached (on pin 10). If not, log.
    if (!SD.begin(10)) {
      Serial.println("SD failed!");
      return;
    }

    // Read the HTML content from SD card into the buffer.
    webFile = SD.open("index.html");
    if (webFile) {
      int bytesRead = 0;
      while (webFile.available() && bytesRead < BUFFER_SIZE - 1) {
        webPageBuffer[bytesRead++] = webFile.read();
      }
      webPageBuffer[bytesRead] = '\0'; 
      webFile.close();
    } else {
      Serial.println("Failed to open index.html!");
      return;
    }

    // Web Server Setup
    // Check if the WiFi module can be reached.
    if (WiFi.status() == WL_NO_MODULE) {
      Serial.println("Communication with WiFi module failed!");
      while (true);
    }
    // Log if still connecting.
    while (status != WL_CONNECTED) {
      Serial.print("Attempting to connect to Network named: ");
      Serial.println(ssid);
      status = WiFi.begin(ssid, pass);
      delay(10000);
    }
    server.begin(); // Begin the server.
    printWifiStatus(); // Once the server is up, call the status function.
  }

  void printWifiStatus() { // A function to print the current WiFi status 
    // Print the network connected to
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // Print the local IP of the Arduino
    IPAddress ip = WiFi.localIP();
    Serial.print("IP Address: ");
    Serial.println(ip);

    // Print instructions to tunnel the server and make it publically accessible (with ngrok)
    Serial.print("To make the page publicly accessible, run the following command in terminal: ngrok http ");
    Serial.print(ip);
    Serial.print(" --domain=zmdesign.work");
    Serial.println(" Once run, the website will be available on https://zmdesign.work");
  }

  // A basic function to convert binary pattern to number. This is used with our touch pad capacitor. I'm sure there is a better way to do this, however I can't seem to figure it out - open to ideas!
  int convertToNumber(const String& binaryPattern) {
    // Perform mapping based on corrected specifications
    if (binaryPattern == "1111111111111111") return 0;
    else if (binaryPattern == "0111111101111111") return 1;
    else if (binaryPattern == "1011111110111111") return 2;
    else if (binaryPattern == "1101111111011111") return 3;
    else if (binaryPattern == "1110111111101111") return 4;
    else if (binaryPattern == "1111011111110111") return 5;
    else if (binaryPattern == "1111101111111011") return 6;
    else if (binaryPattern == "1111110111111101") return 7;
    else if (binaryPattern == "1111111011111110") return 8;
    else return -1; // If no match found
  }

  // Display the typed number on the 8-segment LED display
  void displayNumber(int position, int number) {
    lc.setDigit(0, 7 - position, number, false);
  }

  // Function to send a notification to the MyNotifier App. This is called when the doorbell is pressed.
  void sendNotification() {
    // Initialise a WiFi client to communicate with the server
    WiFiClient client;

    // Check if connection to the server can be established
    if (!client.connect("api.mynotifier.app", 80)) { // Connect to the MyNotifier port
      Serial.println("Connection to the server failed.");
      return;
    }
    
    // Initialise a string to contain the relevant notifcation data, alongside my MyNotifier API Key (Please don't steal it haha)
    String notificationData = "{\"apiKey\":\"7c749b6f-8113-44a9-9f27-6fde4cce08b6\",\"message\":\"Someone's at the door!\",\"description\":\"Tap to see who's here!\",\"body\":\"\",\"type\":\"success\",\"project\":\"\",\"openUrl\":\"https://zmdesign.work\"}";  
    // Send the HTTP request
    client.println("POST / HTTP/1.1");
    client.println("Host: api.mynotifier.app");
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(notificationData.length());
    client.println();
    client.println(notificationData);
    
    // Wait for a response.
    delay(1000);
    
    // Read response from the server and print it to Serial Monitor
    while (client.available()) {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }
    client.stop();
  }

  void loop() {
    // Doorbell Functionality
    // Read the state of the button
    int buttonState = digitalRead(buttonPin);

    // Check if the button state has changed from HIGH to LOW (pressed)
    if (buttonState == LOW && prevButtonState == HIGH) {
      // Log the button press and play a jingle.
      Serial.println("Button pressed");
      tone(piezoPin, 1000); 
      delay(500);
      noTone(piezoPin); 
      tone(piezoPin, 500); 
      delay(500); 
      noTone(piezoPin);
      tone(piezoPin, 1000); 
      delay(500);
      noTone(piezoPin);
      sendNotification(); 
    }

    // Update the previous button state
    prevButtonState = buttonState;

    //WiFi Functionality
    WiFiClient client = server.available(); // Assign incoming clients a to variable

    if (client) { // If the connection has been established...
      Serial.println("New Client");
      String currentLine = "";
      while (client.connected()) { //...and the client is connected...
        if (client.available()) {// ...and the client is available ...
          char c = client.read(); // Read a character from the client
          Serial.write(c);
          if (c == '\n') { // If the character is a new line, check the following line.
            if (currentLine.length() == 0) { // If the following line is empty, we can send our data.
              // Provide a HTTP 200 
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println();

              // Print the contents of the buffer initialised earlier
              client.print(webPageBuffer);
              client.println();
              break; // Break the loop
            } else { // If the line is not empty, go to the next.
              currentLine = "";
            }
          } else if (c != '\r') {
            currentLine += c;
          }

      // Processing inputs from the website. These if statements keep track of HTTP Requests and perform the actions listed on the website
          if (currentLine.endsWith("GET /L")) { 
          lockUnlocked = false;
          Serial.println("Door Locked");
          }
          else if (currentLine.endsWith("GET /U")) {
          lockUnlocked = true;
          Serial.println("Door Unlocked");
          }

          else if (currentLine.startsWith("GET /passcode=")) {
          // Extracting the new passcode from the request
          passcode = currentLine.substring(14, 18);
          Serial.println("Passcode Changed!");
          Serial.println(passcode);
          }
        }
      }
      client.stop();
      Serial.println("Client Disconnected");
    }

  // Touch Capacitor Functionality
  String receivedData;

  // Reading bits from the touch sensor
  for (int i = 0; i < 16; ++i) {
    // Set clock
    digitalWrite(CLOCK_PIN, LOW);
    delayMicroseconds(5); // Reduced delay for faster input
    
    // Read bit
    int databit = digitalRead(DATA_PIN);
    receivedData += (char)('0' + databit); // Store the received bit
    
    // Reset clock
    digitalWrite(CLOCK_PIN, HIGH);   
    delayMicroseconds(50); // Reduced delay for faster input
  }

  // Convert received data to number
  int number = convertToNumber(receivedData);

  // Passcode Functionality
  static String enteredCode; // Static variable to store entered code

  // Process input if not zero and within bounds
  if (number != -1 && number != 0) {
    // Add the digit to entered code
    enteredCode += char('0' + number); 
    Serial.print("Entered Code: ");
    Serial.println(enteredCode);
    
    // Display the entered code on the LED display
    for (int i = 0; i < enteredCode.length(); ++i) {
      displayNumber(i, enteredCode[i] - '0');
    }
    
    // Check if 4 digits are entered
    if (enteredCode.length() == 4) {
      if (enteredCode == passcode && !lockUnlocked) { // Check if entered code matches the passcode and lock is currently locked
        Serial.println("Unlocked");
        lockUnlocked = true;
        digitalWrite(unlockedLedPin, HIGH);
      } else if (enteredCode == "8888" && lockUnlocked) {   // Check if entered code is "8888" and lock is currently unlocked.
        Serial.println("Re-locking");
        lockUnlocked = false; 
        digitalWrite(unlockedLedPin, LOW); 
      } else { // If not, incorrect pass has been entered.
        Serial.println("Incorrect passcode");
        lc.clearDisplay(0); // Clear LED display
        
        // Display 'FALSE' on LED display
        lc.setChar(0, 7, 'f', false);
        lc.setChar(0, 6, 'a', false);
        lc.setChar(0, 5, 'l', false);
        lc.setDigit(0, 4, 5, false); // Use a 5 for s
        lc.setChar(0, 3, 'E', false);
        
        delay(2000);
        lc.clearDisplay(0); 
      }
      enteredCode = ""; 
      lc.clearDisplay(0); 
    }
  }

  // Control servo based on lock state
  if (lockUnlocked) {
    servo.write(90); // Rotate servo to 90 degrees when unlocked
  } else {
    servo.write(0); // Rotate servo to 0 degrees when locked
  }

  // Control LED  based on lock state
  digitalWrite(lockedLedPin, lockUnlocked ? LOW : HIGH);
  digitalWrite(unlockedLedPin, lockUnlocked ? HIGH : LOW);

  delay(1000); 
  }
