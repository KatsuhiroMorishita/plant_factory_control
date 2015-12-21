/***********************************
  kihara_led
porpose: フルカラーテープLEDを4つ制御する。
memo: PWMは4 * 3(R/G/Bで3つ)の12chを制御する。
author: Katsuhiro Morishita
platform: Arduino mega
created: 2015-12-17
lisence: MIT
************************************/
#include <EEPROM.h>

// header
const char header = 0x7f;    // 多数のモジュールを一つのコマンドで操作する（ノード毎のグラデーションがない状況に適している）
const char header_v2 = 0x3f; // 個々のモジュールに発光色を4byteで伝える
const char header_v3 = 0x4f; // 内部時計のリセット（同期用）
const char header_v4 = 0x5f; // オートモードへ切り替え
const char header_v5 = 0x6f; // 遠隔操作モードへ切り替え
const char header_v6 = 0x2f; // 1つのノードが複数の制御対象となるLEDを持っている

// address
const int id_init = -1;
int my_id = id_init;

// com
const long serial_baudrate = 57600;
Stream *control_com;

// LED setup
const float briteness_scale = 0.5f;
const int led_start_pin = 2;
const int led_channel = 4;
const int color_num = 3;
const int port_amount = led_channel * color_num;



void setup() 
{
  // init com
  Serial.begin(serial_baudrate);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  delay(1000);
  Serial.println("-- setup start --");
  
  Serial1.begin(serial_baudrate);
  control_com = &Serial1;
  
    // init rand
  randomSeed(analogRead(A0));
  
  // get ID
  int _id = recieve_id(&Serial);
  if(_id >= 0 && _id != id_init)
  {
    my_id = _id;
    Serial.println("-- write id to EEPROM --");
    byte v_h = (byte)((my_id & 0xff00) >> 8);
    EEPROM.write(0, v_h);
    Serial.println(v_h);
    byte v_l = (byte)(my_id & 0x00ff);
    EEPROM.write(1, v_l);
    Serial.println(v_l);
    int parity = (int)v_h ^ (int)v_l;
    EEPROM.write(2, (byte)parity);
    Serial.println(parity);
    delay(100);
  }
  else
  {
    Serial.println("-- read id from EEPROM --");
    byte v_h = EEPROM.read(0);
    byte v_l = EEPROM.read(1);
    byte v_p = EEPROM.read(2);
    Serial.println(v_h);
    Serial.println(v_l);
    Serial.println(v_p);
    int parity = (int)v_h ^ (int)v_l;
    if(parity == v_p)
      my_id = (int)v_h * 256 + (int)v_l;
    else
      Serial.println("-- parity is not match --");
  }
  Serial.println("-- my ID --");
  Serial.println(my_id);
  
  // ID check
  if(my_id == id_init)
  {
    Serial.println("-- ID error --");
    Serial.println("-- program end --");
    for(;;);
  }
  Serial.println("-- ID check OK --");
}

void loop() 
{
  test2();
  
  /*
  // 受信データ処理
  if(control_com->available())
  {
    int c = control_com->read();
    //Serial.println(c);
    
    if((char)c == header_v6)
    {
      while(1)
      {
        char ans = receive_light_pattern_v2(control_com);
        if(ans != 1)
          break;
      }
    }
  }
  */
}






// timeout check class
class TimeOut
{
  private:
    long timeout_time;
  
  public:
    // set timeout time width
    void set_timeout(long timeout)
    {
      this->timeout_time = millis() + timeout;
    }
    // timeout check, true: timeout
    boolean is_timeout()
    {
      if(millis() > this->timeout_time)
        return true;
      else
        return false;
    }
    // constructer
    TimeOut()
    {
      this->timeout_time = 0l;
    }
};


// millis()の代わりに利用する
// 同期を取りやすくするため
class kmTimer
{
private:
  long _time_origin;

public:
  void reset()
  {
    this->_time_origin = millis();
  }
  long km_millis()
  {
    return millis() - this->_time_origin;
  }
  // constructer
  kmTimer()
  {
    this->_time_origin = 0l;
  }
};



/** global variabls part 2 **********************/
kmTimer kmtimer;
// millis()を置換させる（同期をとるため）
#define millis() kmtimer.km_millis()



/** general functions ***************************/

// IDを受信する
// IDは文字列で受信する。
// 例："1234"
int recieve_id(Stream *_port)
{
  TimeOut to;
  int id = id_init;
  //Stream *_port = &port;

  long wait_time_ms = 5000l;
  _port->print("please input ID. within ");
  _port->print(wait_time_ms);
  _port->println(" ms.");
  
  to.set_timeout(wait_time_ms);
  _port->flush();
  while(to.is_timeout() == false)
  {
    if(_port->available())
    {
      int c = _port->read();
      //_port->print("r: ");
      //_port->println(c);
      if (c == 0x0a || c == 0x0d)
        break;
      if (c < '0' || c > '9')
      {
        _port->print("-- Error: non number char was received --");
        break;
      }
      if (id == id_init)
        id = 0;
      to.set_timeout(50);            // 一度受信が始まったらタイムアウトを短くして、処理時間を短縮する
      id = id * 10 + (c - '0');
      if(id >= 3200)                 // 3200以上の数値を10倍するとint型の最大値を超える可能性がある.負数は無視。
        break;
    }
  }
  if(id != id_init)
    _port->print("-- ID was received --");
  //_port->print("r ID: ");
  //_port->println(id);
  return id;
}

// LED control
void light(char light_arg[], int _size)
{ 
  for(int i = 0; i < _size; i++)
  {
    analogWrite(led_start_pin + i, light_arg[i]);
    Serial.print(light_arg[i]);
    Serial.print(",");
  }
  Serial.println("");
  return;
}



// recieve light pattern v2
// return: char, 1: 再度呼び出しが必要
char receive_light_pattern_v2(Stream *port)
{
  char ans = 2;
  int my_index = my_id;
  TimeOut to;
  int index = 0;
  char buff[port_amount];
  
  for(int i = 0; i < port_amount; i++)
    buff[i] = 0;
  
  Serial.println("-- p2 --");
  to.set_timeout(20);
  while(to.is_timeout() == false)
  {
    if(port->available())
    {
      int c = port->read();
      //Serial.println(c);
      if(c == header_v6)          // check header
      {
        //Serial.println("fuga");
        ans = 1;
        break;                    // 再帰は避けたい
      }
      if((c & 0x80) == 0)         // プロトコルの仕様上、ありえないコードを受信
      {
        //Serial.println("hoge");
        ans = 2;
        break;                    // エラーを通知して、関数を抜ける
      }
      c = c & 0x7f;               // 最上位ビットにマスク
      to.set_timeout(20);
      
      if(index < port_amount)
        buff[index] = (char)(c << 1);
      
      if (index == port_amount && c == my_index)
      {
        light(buff, port_amount);
        ans = 0;
        break;
      }
      index += 1;
    }
  }
  //if(to.is_timeout() == true)
  //  Serial.println("time out.");
  return ans;
}


void test()
{
  char buff[port_amount];
  int k = 0;
  
  while(1)
  {
    for(int i = 0; i < port_amount;i++)
      buff[i] = 0;
    buff[k] = 255;
    light(buff, port_amount);
    delay(200);
    k = (k + 1) % port_amount;
  }
  return;
}

void test2()
{
  char buff[port_amount];
  int k = 0;
  const long interval = 5000l;
  
  while(1)
  {
    long t = millis();
    float amp = sin((float)(t % interval) / (float)interval * 3.14 * 2.0) + 1.0;
    for(int i = 0; i < port_amount;i++)
      buff[i] = (char)(127 * amp);
    light(buff, port_amount);
    //delay(200);
    k = (k + 1) % port_amount;
  }
  return;
}

