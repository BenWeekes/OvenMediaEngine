## Usage 

* Publish main content from OBS with rtmp://vr-demo.agora.io:1935/app/ and stream key equal to 'stream'     
* Publish commentator with webrtc at https://vr-demo.agora.io/ome/OvenPlayer/demo/demo_input.html      
    input: wss://vr-demo.agora.io:3334/commentator/stream?direction=send     
    output: wss://vr-demo.agora.io:3334/app/stream_o     
* Play the mixed ll-hls at https://www.theoplayer.com/test-your-stream-hls-dash-hesp      
    custom stream: https://vr-demo.agora.io:3334/app/stream/llhls.m3u8      

## Build, Run & Log 

cd ~/OME-Fork/OvenMediaEngine/src
./restart

## Config 

 sudo vi /usr/share/ovenmediaengine/conf/Server.xml

## Status 

 systemctl status ovenmediaengine.service
