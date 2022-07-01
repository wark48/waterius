
#include "setup_ap.h"
#include "Logging.h"
#include "wifi_settings.h"

#include <ESP8266WiFi.h>
#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiClient.h>
#include <EEPROM.h>
#include "utils.h"
#include "WateriusHttps.h"
#include "master_i2c.h"
#include "porting.h"

#define AP_NAME "watermer_" FIRMWARE_VERSION

extern SlaveData data;
extern MasterI2C masterI2C;

SlaveData runtime_data;


#define IMPULS_LIMIT_1  3  // Если пришло импульсов меньше 3, то перед нами 10л/имп. Если больше, то 1л/имп.

uint8_t get_auto_factor(uint32_t runtime_impulses, uint32_t impulses)
{
    return (runtime_impulses - impulses <= IMPULS_LIMIT_1) ? 10 : 1;
}

uint8_t get_factor(uint8_t combobox_factor, uint32_t runtime_impulses, uint32_t impulses, uint8_t cold_factor) {

    switch (combobox_factor) {
        case AUTO_IMPULSE_FACTOR: return get_auto_factor(runtime_impulses, impulses); 
        case AS_COLD_CHANNEL: return cold_factor;
        default: 
            return combobox_factor;  // 1, 10, 100
    }
}

#define SETUP_TIME_SEC 600UL //На какое время Attiny включает ESP (файл Attiny85\src\Setup.h)
void update_data(String &message)
{
    if (masterI2C.getSlaveData(runtime_data)) {
        String state0good(F("\"\""));
        String state0bad(F("\"Не подключён\""));
        String state1good(F("\"\""));
        String state1bad(F("\"Не подключён\""));

        uint32_t delta0 = runtime_data.impulses0 - data.impulses0;
        uint32_t delta1 = runtime_data.impulses1 - data.impulses1;
        
        if (delta0 > 0) {
            state0good = F("\"Подключён\"");
            state0bad = F("\"\"");
        }
        if (delta1 > 0) {
            state1good = F("\"Подключён\"");
            state1bad = F("\"\"");
        }

        message = F("{\"state0good\": ");
        message += state0good;
        message += F(", \"state0bad\": ");
        message += state0bad;
        message += F(", \"state1good\": ");
        message += state1good;
        message += F(", \"state1bad\": ");
        message += state1bad;
        message += F(", \"elapsed\": \"");
        message += String((uint32_t)(SETUP_TIME_SEC - millis()/1000.0));
        message += F(" сек. \", \"fc_feedback\": \"Вес импульса ");
        message += String(get_auto_factor(runtime_data.impulses1, data.impulses1));
        message += F(" л/имп\", \"fh_feedback\": \"Вес импульса ");
        message += String(get_auto_factor(runtime_data.impulses0, data.impulses0));
        message += F(" л/имп\", \"error\": \"\"");
        message += F("}");
    }
    else {
        message = F("{\"error\": \"Ошибка связи с МК\","
                    "\"elapsed\": \"111 сек\","
                    "\"state0good\": \"\","
                    "\"state1good\": \"Подключён\","
                    "\"state0bad\": \"Не подключён\","
                    "\"state1bad\": \"\","
                    "\"fc_feedback\": \"Вес импульса 1 л/имп\", "
                    "\"fh_feedback\": \"Вес импульса 1 л/имп\"}");
    }
}

WiFiManager wm;
void handleStates(){
  LOG_INFO(F("/states request"));
  String message;
  message.reserve(300); //сейчас 200
  update_data(message);
  wm.server->send(200, F("text/plain"), message);
}

void handleNetworks() {
  LOG_INFO(F("/networks request"));
  String message;
  message.reserve(4000);
  wm.WiFi_scanNetworks(wm.server->hasArg(F("refresh")),false); //wifiscan, force if arg refresh
  wm.getScanItemOut(message);  
  wm.server->send(200, F("text/plain"), message);
}

void bindServerCallback(){
  wm.server->on(F("/states"), handleStates);
  wm.server->on(F("/networks"), handleNetworks);
}


void setup_ap(Settings &sett, const SlaveData &data, const CalculatedData &cdata) 
{
    wm.debugPlatformInfo();
    wm.setWebServerCallback(bindServerCallback);

    LOG_INFO(F("User requested captive portal"));
    
    // Настройки HTTP 

    WiFiManagerParameter title_email("<h3>Электронная почта с watermer.ru</h3><div class='form-inner-wrap'><p class='text-light'>Заполните, email указанный при регистрации на сайте или в мобильном приложении, чтобы увидеть показания</p>");
    wm.addParameter(&title_email);

    EmailParameter param_waterius_email("wmail", "Электронная почта",  sett.waterius_email, EMAIL_LEN-1);
    wm.addParameter(&param_waterius_email);

    // Чекбокс доп. настроек

    WiFiManagerParameter checkbox("<label class='cnt cnt-toggle'>Дополнительные настройки<input type='checkbox' id='chbox' name='chbox' onclick='advSett()'><span class='mrk'></span></label>");
    wm.addParameter(&checkbox);

    WiFiManagerParameter div_start("<div id='advanced' style='display:none'>");  //form-inner-wrap
    wm.addParameter(&div_start);

    // Сервер http запроса 
    WiFiManagerParameter param_waterius_host( "whost", "Адрес сервера (включает отправку)",  sett.waterius_host, WATERIUS_HOST_LEN-1);
    wm.addParameter(&param_waterius_host);

    //ShortParameter param_wakeup_per("mperiod", "Период отправки показаний, мин.",  sett.wakeup_per_min);
    //wm.addParameter(&param_wakeup_per);

    // Настройки MQTT
    
    WiFiManagerParameter label_mqtt("<h3>MQTT</h3>");
    wm.addParameter(&label_mqtt);
    WiFiManagerParameter param_mqtt_host( "mhost", "Адрес сервера (включает отправку)<br/>Пример: broker.hivemq.com",  sett.mqtt_host, MQTT_HOST_LEN-1);
    wm.addParameter(&param_mqtt_host );

    LongParameter param_mqtt_port( "mport", "Порт",  sett.mqtt_port);
    wm.addParameter(&param_mqtt_port );
    WiFiManagerParameter param_mqtt_login( "mlogin", "Логин",  sett.mqtt_login, MQTT_LOGIN_LEN-1);
    wm.addParameter(&param_mqtt_login );
    WiFiManagerParameter param_mqtt_password( "mpassword", "Пароль",  sett.mqtt_password, MQTT_PASSWORD_LEN-1);
    wm.addParameter(&param_mqtt_password );
    WiFiManagerParameter param_mqtt_topic( "mtopic", "Topic",  sett.mqtt_topic, MQTT_TOPIC_LEN-1);
    wm.addParameter(&param_mqtt_topic );
    
    // Статический ip
    
    WiFiManagerParameter label_network("<h3>Сетевые настройки</h3>");
    wm.addParameter(&label_network);
    
    String mac("<label class=\"label\">MAC: ");
    mac += WiFi.macAddress();
    mac += "</label>";
    WiFiManagerParameter label_mac(mac.c_str());
    wm.addParameter(&label_mac);

    IPAddressParameter param_ip("ip", "Статический ip<br/>(DHCP, если равен 0.0.0.0)",  sett.ip);
    wm.addParameter(&param_ip);
    
    WiFiManagerParameter frm_row("<div class='frm-row'>");
    wm.addParameter(&frm_row);

    WiFiManagerParameter field_half1("<div class='frm-field field-half'>");
    wm.addParameter(&field_half1);
    IPAddressParameter param_gw("gw", "Шлюз",  sett.gateway);
    wm.addParameter(&param_gw);
    WiFiManagerParameter field_half1_end("</div>");
    wm.addParameter(&field_half1_end);

    WiFiManagerParameter field_half2("<div class='frm-field field-half'>");
    wm.addParameter(&field_half2);
    IPAddressParameter param_mask("sn", "Маска подсети",  sett.mask);
    wm.addParameter(&param_mask);
    WiFiManagerParameter field_half2_end("</div>");
    wm.addParameter(&field_half2_end);

    WiFiManagerParameter frm_row_end("</div>");
    wm.addParameter(&frm_row_end);

    WiFiManagerParameter label_factor_settings("<h3>Параметры счетчиков</h3>");
    wm.addParameter(&label_factor_settings);

    WiFiManagerParameter label_cold_factor("<div class='label'>Холодная вода л/имп</div>");
    wm.addParameter(&label_cold_factor);
    
    DropdownParameter dropdown_cold_factor("factorCold");
    dropdown_cold_factor.add_option(AUTO_IMPULSE_FACTOR, "Авто", sett.factor1);
    dropdown_cold_factor.add_option(1, "1", sett.factor1);
    dropdown_cold_factor.add_option(10, "10", sett.factor1);
    dropdown_cold_factor.add_option(100, "100", sett.factor1);
    wm.addParameter(&dropdown_cold_factor);

    WiFiManagerParameter label_factor_cold_feedback("<div class='label' id='fc_fb_control'><div id='fc_feedback'></div></div>");
    wm.addParameter(&label_factor_cold_feedback);

    WiFiManagerParameter label_hot_factor("<div class='label'>Горячая вода л/имп</div>");
    wm.addParameter(&label_hot_factor);
    
    DropdownParameter dropdown_hot_factor("factorHot");
    dropdown_hot_factor.add_option(AS_COLD_CHANNEL, "Как у холодной", sett.factor0);
    dropdown_hot_factor.add_option(AUTO_IMPULSE_FACTOR, "Авто", sett.factor0);
    dropdown_hot_factor.add_option(1, "1", sett.factor0);
    dropdown_hot_factor.add_option(10, "10", sett.factor0);
    dropdown_hot_factor.add_option(100, "100", sett.factor0);
    wm.addParameter(&dropdown_hot_factor);

    WiFiManagerParameter label_factor_hot_feedback("<div class='label' id='fh_fb_control'><div id='fh_feedback'></div></div>");
    wm.addParameter( &label_factor_hot_feedback);

    // конец доп. настроек
    WiFiManagerParameter div_end("</div></div>");  //form-inner-wrap + id="advanced"
    wm.addParameter(&div_end);
    
    // Счетчиков
    WiFiManagerParameter cold_water("<h3><div class='elm-cold'></div>Холодная вода</h3>");
    wm.addParameter(&cold_water);
            
    WiFiManagerParameter label_cold_info("<p class='text-light'>Во время первой настройки спустите унитаз 1&ndash;3 раза (или вылейте не&nbsp;меньше 4&nbsp;л.), пока надпись не&nbsp;сменится на&nbsp;&laquo;подключен&raquo;.<br><br>Если статус &laquo;не&nbsp;подключен&raquo;, проверьте провод в&nbsp;разъёме. Ватериус так определяет тип счётчика. Счётчики не влияют на связь сервером.</p>");
    wm.addParameter( &label_cold_info);

    WiFiManagerParameter label_cold_state("<b><p class='no-good' id='state1bad'></p><p class='good' id='state1good'></p></b>");
    wm.addParameter( &label_cold_state);

    WiFiManagerParameter label_cold("<label class='cold label' for='ch1'>Показания холодной воды</label>");
    wm.addParameter( &label_cold);
    FloatParameter param_channel1_start("ch1", "",  cdata.channel1);
    wm.addParameter( &param_channel1_start);
    
    WiFiManagerParameter param_serial_cold("serialCold", "серийный номер",  sett.serial1, SERIAL_LEN-1);
    wm.addParameter(&param_serial_cold);

    WiFiManagerParameter hot_water("<h3><div class='elm-hot'></div>Горячая вода</h3>");
    wm.addParameter(&hot_water);
            
    WiFiManagerParameter label_hot_info("<p class='text-light'>Откройте кран горячей воды, пока надпись не&nbsp;сменится на&nbsp;&laquo;подключен&raquo;</p>");
    wm.addParameter( &label_hot_info);
    
    WiFiManagerParameter label_hot_state("<b><p class='no-good' id='state0bad'></p><p class='good' id='state0good'></p></b>");
    wm.addParameter( &label_hot_state );

    WiFiManagerParameter label_hot("<label class='hot label' for='ch0'>Показания горячей воды</label>");
    wm.addParameter( &label_hot);
    FloatParameter param_channel0_start("ch0", "",  cdata.channel0);
    wm.addParameter( &param_channel0_start);

    WiFiManagerParameter param_serial_hot("serialHot", "серийный номер",  sett.serial0, SERIAL_LEN-1);
    wm.addParameter(&param_serial_hot);

    wm.setConfigPortalTimeout(SETUP_TIME_SEC);
    wm.setConnectTimeout(ESP_CONNECT_TIMEOUT);
    
    LOG_INFO(F("Start ConfigPortal"));

    // Запуск веб сервера на 192.168.4.1
    LOG_INFO(F("chip id:") << getChipId());
    
    /*
    String ap_name = AP_NAME "_" + String(getChipId(), HEX).substring(0, 4);
    ap_name.toUpperCase();
    wm.startConfigPortal(ap_name.c_str());
    */
    wm.startConfigPortal(AP_NAME);

    // Успешно подключились к Wi-Fi, можно засыпать
    LOG_INFO(F("Connected to wifi. Save settings, go to sleep"));

    // Переписываем введенные пользователем значения в Конфигурацию

    strncpy0(sett.waterius_email, param_waterius_email.getValue(), EMAIL_LEN);
    strncpy0(sett.waterius_host, param_waterius_host.getValue(), WATERIUS_HOST_LEN);

    // Генерируем ключ используя и введенную эл. почту
    if (strnlen(sett.waterius_key, WATERIUS_KEY_LEN) == 0) {
        LOG_INFO(F("Generate waterius key"));
        WateriusHttps::generateSha256Token(sett.waterius_key, WATERIUS_KEY_LEN, 
                                           sett.waterius_email);
    }

    strncpy0(sett.mqtt_host, param_mqtt_host.getValue(), MQTT_HOST_LEN);
    strncpy0(sett.mqtt_login, param_mqtt_login.getValue(), MQTT_LOGIN_LEN);
    strncpy0(sett.mqtt_password, param_mqtt_password.getValue(), MQTT_PASSWORD_LEN);
    strncpy0(sett.mqtt_topic, param_mqtt_topic.getValue(), MQTT_TOPIC_LEN);
    sett.mqtt_port = param_mqtt_port.getValue();   

    sett.ip = param_ip.getValue();
    sett.gateway = param_gw.getValue();
    sett.mask = param_mask.getValue();
    
    //период отправки данных
    //sett.wakeup_per_min = param_wakeup_per.getValue();
    //sett.set_wakeup = sett.wakeup_per_min;
    //LOG_INFO("wakeup period, min=" << sett.wakeup_per_min);
    //LOG_INFO("wakeup period, tick=" << sett.set_wakeup);

    //Веса импульсов
    LOG_INFO("hot dropdown=" << dropdown_hot_factor.getValue());
    LOG_INFO("cold dropdown=" << dropdown_cold_factor.getValue());
    
    uint8_t combobox_factor = dropdown_cold_factor.getValue();
    sett.factor1 = get_factor(combobox_factor, runtime_data.impulses1, data.impulses1, 1);
    
    combobox_factor = dropdown_hot_factor.getValue();
    sett.factor0 = get_factor(combobox_factor, runtime_data.impulses0, data.impulses0, sett.factor1);

    strncpy0(sett.serial0, param_serial_hot.getValue(), SERIAL_LEN);
    strncpy0(sett.serial1, param_serial_cold.getValue(), SERIAL_LEN);

    // Текущие показания счетчиков
    sett.channel0_start = param_channel0_start.getValue();
    sett.channel1_start = param_channel1_start.getValue();

    //sett.liters_per_impuls_hot = 
    LOG_INFO("factorHot=" << sett.factor0);
    LOG_INFO("factorCold=" << sett.factor1);

    // Запоминаем кол-во импульсов Attiny соответствующих текущим показаниям счетчиков
    sett.impulses0_start = runtime_data.impulses0;
    sett.impulses1_start = runtime_data.impulses1;

    // Предыдущие показания счетчиков. Вносим текущие значения.
    sett.impulses0_previous = sett.impulses0_start;
    sett.impulses1_previous = sett.impulses1_start;

    LOG_INFO("impulses0=" << sett.impulses0_start );
    LOG_INFO("impulses1=" << sett.impulses1_start );

    sett.setup_time = millis();
    sett.setup_finished_counter++;

    sett.crc = FAKE_CRC; // todo: сделать нормальный crc16
    storeConfig(sett);
}
