use std::{collections::HashMap, fs, sync::{Arc, LazyLock}};
use chrono::Timelike;
use reqwest::Url;
use tokio::task;
use tokio::time::Duration;
use tokio::time;
use axum::{body::Body, http::{header, StatusCode}, response::IntoResponse, routing::{get, post, put}, Json, Router};
use serde::{de::Visitor, Deserialize, Serialize};
use tokio::sync::Mutex;
use tower_http::{cors::CorsLayer, services::{ServeFile, ServeDir}};




#[derive(Default)]
struct GlobalState {
    imgdata: Option<ImageData>,
    img_debug_data: Option<ImageData>,
    acdata: ACData,
}

#[derive(Default, Debug)]
struct ThingspeakState {
    ir_estimation: Option<f32>,
    tof_count: Option<i32>,
    weather: Option<WeatherDatum>,
}

impl ThingspeakState {
    fn to_url_params(&self) -> Vec<(String, String)> {
        let mut v = vec![];
        if let Some(x) = self.ir_estimation {
            v.push(("field5", x.to_string()));
        }
        if let Some(x) = self.tof_count {
            v.push(("field6", x.to_string()));
        }
        if let Some(weather) = self.weather {
            v.push(("field1", weather.temperature.to_string()));
            v.push(("field2", weather.pressure.to_string()));
            v.push(("field3", weather.humidity.to_string()));
            v.push(("field4", weather.gas_resistance.to_string()));
        }
        v.into_iter().map(|(k,v)| (k.to_owned(), v)).collect()
    }
}


static STATE: LazyLock<Arc<Mutex<GlobalState>>> = LazyLock::new(|| Default::default());
static OLED: LazyLock<Arc<Mutex<String>>> = LazyLock::new(|| Arc::new(Mutex::new("---".to_owned())));
static THING: LazyLock<Arc<Mutex<ThingspeakState>>> = LazyLock::new(|| Default::default());

const URL: &'static str = "https://api.thingspeak.com/update";
static API_KEY: LazyLock<&'static str> = LazyLock::new(|| {
    dotenv::var("api").unwrap().leak()
});
fn api_param() -> (String, String) {
    return ("api_key".to_owned(), API_KEY.to_owned());
}


async fn oled() -> impl IntoResponse {
    println!("oled");
    (StatusCode::OK, Body::new(OLED.lock().await.clone()))
    //(StatusCode::OK, "plus".to_owned());
}

async fn post_log(req: String) -> impl IntoResponse {
    println!("#########");
    println!("Received string\n---------");
    println!("{}", req);
    println!("#########");
    return StatusCode::OK;	
}
#[tokio::main]
async fn main() {
    // build our application with a route
    let app = Router::new()
        .layer(CorsLayer::very_permissive())
        .route("/img", post(set_image))
        .route("/img", get(get_image))
        .route("/log", post(post_log))
        .route("/oled", get(oled))
        .route("/img_data", get(get_image_data))
        .route("/img_debug_data", get(get_image_debug_data))
        .route("/laser", post(laser))
        .route_service("/", ServeFile::new("./index.html"))
        .route_service("/ota.json", ServeFile::new("./ota.json"))
        .layer(CorsLayer::very_permissive())
        .route_service("/index.html", ServeFile::new("./index.html"))
        .route_service("/index.css", ServeFile::new("./index.css"))
        .route_service("/index.js", ServeFile::new("./index.js"))
        .nest_service("/ota", ServeDir::new("/home/gamma/images/ota"))
        .route("/aircon", put(put_aircon))
        .route("/aircon", get(get_aircon))
        .route("/weather", put(set_weather))
        .layer(CorsLayer::very_permissive())
    ;
    let listener = tokio::net::TcpListener::bind("0.0.0.0:5000").await.unwrap();
    let forever = task::spawn(async {
    println!("Foreverrrrr");
        let mut interval = time::interval(Duration::from_millis(30000));
        loop {
            interval.tick().await;
        let mut lock = THING.lock().await;
        let url = Url::parse_with_params(URL, {let mut p = lock.to_url_params(); p.push(api_param()); p}).unwrap();
        let resp = reqwest::get(url).await.unwrap().text().await.unwrap();
        println!("Sent fields {:?}, got {:?}", lock, resp);
            lock.tof_count = None;
        }
    });
    let _ = tokio::join!(
        axum::serve(listener, app),
    forever
        
    );


    // run it
}
#[derive(Debug, Deserialize, Serialize, Clone, Copy)]
#[serde(rename_all = "lowercase")] 
enum ACMode {
    Cool,
    Heat
}

#[repr(C)]
#[derive(Debug, Deserialize, Serialize, Clone, Copy)]
struct ACData {
    protocol: i32,
    temperature: i32,
    power: bool,
    mode: ACMode,
    force_update: bool,
}



#[derive(Debug, Deserialize, Clone)]
struct ACUpdate {
    protocol: i32,
    temperature: TemperatureUpdate,
    power: bool,
    mode: ACMode,
    password: String
}
#[derive(Debug, Clone, Copy)]
enum TemperatureUpdate {
    Set(i32),
    Increase,
    Decrease,
    Current
}

struct TemperatureUpdateVisitor;

impl <'de>Visitor<'de> for TemperatureUpdateVisitor {
    type Value = TemperatureUpdate;
    fn visit_i64<E>(self, v: i64) -> Result<Self::Value, E>
        where
            E: serde::de::Error, {
        Ok(TemperatureUpdate::Set(v as i32))
    }
    fn visit_u64<E>(self, v: u64) -> Result<Self::Value, E>
        where
            E: serde::de::Error, {
        Ok(TemperatureUpdate::Set(v as i32))
    }
    fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
        where
            E: serde::de::Error, {
        match v {
            "increase" | "up" => Ok(TemperatureUpdate::Increase),
            "decrease" | "down" => Ok(TemperatureUpdate::Decrease),
            "current" => Ok(TemperatureUpdate::Current),
            _ => todo!()
        }
    }
    fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
        formatter.write_str("Expecting temperature change.")
    }
}

impl <'de>Deserialize<'de> for TemperatureUpdate {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
        where
            D: serde::Deserializer<'de> {
                println!("asd");
        deserializer.deserialize_any(TemperatureUpdateVisitor)
    }
}


impl Default for ACData {
    fn default() -> Self {
        Self {
            protocol: 18,
            temperature: 25,
            power: false,
        mode: ACMode::Heat,
        force_update: false
        }
    }
}



async fn put_aircon(data: Json<ACUpdate>) -> impl IntoResponse {
    if data.password != dotenv::var("acpass").unwrap_or_default() {
        return StatusCode::FORBIDDEN.into_response();
    }
    let mut l = STATE.lock().await;
    l.acdata.power = data.power;
    l.acdata.protocol = data.protocol;
    l.acdata.mode = data.mode;
    match data.temperature {
        TemperatureUpdate::Set(x) => {
            l.acdata.temperature = x;
        },
        TemperatureUpdate::Increase => {
            l.acdata.temperature += 1;
        },
        TemperatureUpdate::Decrease => {
            l.acdata.temperature -= 1;
        }
        _ => {}
    };
    (StatusCode::OK, Body::new(serde_json::to_string(&l.acdata).unwrap())).into_response()
}

async fn get_aircon() -> impl IntoResponse {
    let mut acstate = STATE.lock().await.acdata;
    // let ts = THING.lock().await;
    // if ts.weather.map(|w| w.temperature < 15.0).unwrap_or_default() && ts.ir_estimation.unwrap_or_default() > 4.0 {
    //     acstate = ACData {
    //         mode: ACMode::Heat,
    //         temperature: 21,
    //         power: true,
    //         force_update: true,
    //         ..Default::default()
    //     }
    // }
    let datetime = chrono::Utc::now();
    let time = datetime.time();
    let (h,m) = (time.hour(), time.minute());
    if h == 3 && m == 0 {
        acstate = ACData {
            power: false,
            force_update: true,
            ..Default::default()        
        }
    }
    (StatusCode::OK, Body::from(serde_json::to_string(&acstate).unwrap()))
}





#[derive(Debug, Deserialize, Serialize, Clone, Default)]
struct ImageData {
    width: u32,
    height: u32,
    data: Vec<f64>,
    temp: f64
}


const fn normalize(temp: f64, thr: f64) -> f64 {
    const TEMP_RANGE: f64 = 10.0;
    let x = (temp-thr) / TEMP_RANGE;
    if x < 0.0 {0.0} else if x > 1.0 {1.0} else {x}
}

async fn get_image_data() -> impl IntoResponse {
    (StatusCode::OK, Body::new(serde_json::to_string(STATE.lock().await.imgdata.as_ref().unwrap()).unwrap()))
}
async fn get_image_debug_data() -> impl IntoResponse {
    (StatusCode::OK, Body::new(serde_json::to_string(STATE.lock().await.img_debug_data.as_ref().unwrap()).unwrap()))
}
async fn get_image() -> impl IntoResponse {
    let file = fs::read("test.png").unwrap();
    let content_type = "image/png";


    let headers = [
        (header::CONTENT_TYPE, content_type),
        (
            header::CONTENT_DISPOSITION,
            "attachment; filename=\"test.png\""
        ),
    ];
    (headers, file)
}


#[derive(Debug, Deserialize, Serialize, Clone, Default)]
struct ImageDayData(HashMap<String, ImageData>);

async fn get_estimation(data: &ImageData) -> f32{
    let ta = data.temp * dotenv::var("tempsub").unwrap().parse::<f64>().unwrap();
    let mut ddata = ImageData {
        width: 32,
        height: 24,
        temp: 0.0,   
        data: vec![]
    };
    let mut est = 0.0;
    for h in 0..24 {
        for w in 0..32 {
            let idx = h*32 + w;
            let mult: f32 = match 24-h {
                 0..8    => dotenv::var("mult1").unwrap().parse().unwrap(),
                 8..13   => dotenv::var("mult2").unwrap().parse().unwrap(),
                 13..18  => dotenv::var("mult3").unwrap().parse().unwrap(),
                 _       => dotenv::var("mult4").unwrap().parse().unwrap()
            };
            let v: f32 = if data.data[idx] > ta {1.0} else {0.0} * mult;
            est += v;
            ddata.data.push(v as f64);
        }
    }
    est
}

async fn set_image(body: String) -> impl IntoResponse {
    
    task::spawn(async move {
        process_image(body).await;
    });
    return StatusCode::OK;
}


use tokio::io::AsyncReadExt;
use tokio::io::AsyncWriteExt;
async fn process_image(body: String) {
    //println!("img");
    let imgdata: ImageData = serde_json::from_str(&body).unwrap();
    let ta = imgdata.temp;
    let est = get_estimation(&imgdata).await;
    //println!("img2");
    println!("img temp {}, est {}", ta, est);
    let mut lock = STATE.lock().await;
    lock.imgdata = Some(imgdata.clone());
    let _ = lock;
    THING.lock().await.ir_estimation = Some(est);

    
    let utc = chrono::Utc::now();
    let dt = utc.date_naive();
    let filename = format!("{}_image.txt", dt.to_string());
    // println!("{:?}", dt.to_string());
    let mut file = tokio::fs::OpenOptions::new()
        .create(true)
        .write(true)
        .read(true)
        .append(false)
        .open(&filename).await.unwrap();
    let mut st = String::new();
    let _ = file.read_to_string(&mut st).await.unwrap();
    let _ = file.flush();
    let _ = file;

    let mut data: ImageDayData = serde_json::from_str(&st).unwrap_or_default();
    data.0.insert(utc.time().to_string(), imgdata.clone());
    let wst = serde_json::to_string(&data).unwrap();
    tokio::fs::write(filename, &wst.bytes().collect::<Vec<u8>>()).await.unwrap();


    let thr = ta * dotenv::var("tempsub").unwrap().parse::<f64>().unwrap();
    let data = image::RgbaImage::from_raw(imgdata.width, imgdata.height, imgdata.data.into_iter().flat_map(|px| {
        let ar = colorous::VIRIDIS.eval_continuous(normalize(px, thr)).as_array();
        [ar[0], ar[1], ar[2], 255]
    }).collect::<Vec<_>>());
    let data = data.unwrap();
    let data = image::imageops::flip_horizontal(&data);
    let _ = data.save("ir.png");
}


#[derive(Debug, Deserialize, Serialize, Clone, Default)]
struct LaserData {
    left: Vec<i32>,
    right: Vec<i32>
}
#[derive(Debug, Deserialize, Serialize, Clone, Default)]
struct LaserDayData(HashMap<String, LaserData>);


fn est(x: i32) -> i32 {
    if x < 5 {1}
    else {
        1 + (x-5)/10
    }
}


fn count(datum: LaserData) -> Vec<i32> {
    let mut c = vec![];
    let mut pol = 0;
    let mut pc = 0;
    for i in 0..datum.left.len() {
        let a = datum.left[i];
        let b = datum.right[i];
        let ab = a < 1500; // threshold de 1500mm de distancia
        let bb = b < 1500;
        match (ab, bb) {
            (false, false) => {
                if pc > 0 {c.push(est(pc) * pol)};
                pc = 0;
                pol = 0;
            },
            (true, false) => {
                if pc > 0 {c.push(est(pc) * pol)};
                pc = 0;
                pol = 1;
            },
            (false, true) => {
                if pc > 0 {c.push(est(pc) * pol)};
                pc = 0;
                pol = -1;
            },
            (true, true) => {
                pc += 1;
            },
        }
    }
    c
}


async fn laser(datum: Json<Vec<i32>>) -> impl IntoResponse {
    println!("laser");

    use tokio::io::AsyncReadExt;
    use tokio::io::AsyncWriteExt;
    let utc = chrono::Utc::now();
    let dt = utc.date_naive();

    let filename = format!("{}_laser.txt", dt.to_string());
    let mut file = tokio::fs::OpenOptions::new()
        .create(true)
        .write(true)
        .read(true)
        .append(false)
        .open(&filename).await.unwrap();
    let mut st = String::new();
    let _ = file.read_to_string(&mut st).await.unwrap();
    let _ = file.flush();
    let _ = file;
    let mut data: LaserDayData = serde_json::from_str(&st).unwrap_or_default();
    let (left, right) = datum.0.chunks(2).map(|x| (x[0], x[1])).collect();
    let datum = LaserData {left, right};
    data.0.insert(utc.time().to_string(), datum.clone());
    let count = count(datum).into_iter().sum::<i32>();    
    let mut lock = THING.lock().await;
    lock.tof_count = lock.tof_count.map(|x| x + count);
    *OLED.lock().await = format!("{}", count.to_string());
    let wst = serde_json::to_string(&data).unwrap();
    tokio::fs::write(filename, &wst.bytes().collect::<Vec<u8>>()).await.unwrap();
    StatusCode::OK  
}


#[derive(Debug, Deserialize, Serialize, Clone, Default, Copy)]
struct WeatherDatum {
    temperature: f32,
    humidity: f32,
    pressure: f32,
    gas_resistance: f32
}


async fn set_weather(datum: Json<WeatherDatum>) -> impl IntoResponse {
    println!("mreceived weather data {:?}", datum);
    THING.lock().await.weather = Some(datum.0);
    StatusCode::OK
}
