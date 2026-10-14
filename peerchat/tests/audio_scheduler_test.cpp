// Reuse the existing fake WinMM boundary, but execute the real Audio worker.
#define main preserved_audio_lifecycle_main
#include "audio_lifecycle_test.cpp"
#undef main
int main(int argc,char** argv){try{
    fake::renderTest=true;fake::outputWrites=0;
    peerchat::Audio audio;audio.setDevices(0,0);
    std::atomic<unsigned> captures{0},atFirst{999},playouts{0};std::atomic<long long> firstUs{0};
    const auto start=std::chrono::steady_clock::now();
    audio.setCaptureHandler([&](const std::vector<unsigned char>&){++captures;std::this_thread::sleep_for(std::chrono::milliseconds(25));});
    audio.setPlayoutHandler([&](std::int16_t* pcm,std::size_t n){if(playouts.fetch_add(1)==0){atFirst=captures.load();firstUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();}std::fill(pcm,pcm+n,1000);return true;});
    std::wstring error;require(audio.start(error),"Scheduler fixture start");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while((captures<4||playouts<3)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    audio.stop();require(captures==4&&playouts==3&&fake::inputs==0&&fake::outputs==0&&fake::invalidQueues==0,"Scheduler fixture lifecycle");
    bool optimized=argc>1&&std::string(argv[1])=="optimized";
    require(optimized?atFirst==0:atFirst==4,"Playback/capture service ownership changed unexpectedly");
    std::cout<<"AUDIO_BACKLOG_FIXTURE mode="<<(optimized?"interleaved":"capture-first")<<" capture_work_ms=25 ready_capture_buffers=4 capture_callbacks_before_first_playout="<<atFirst<<" start_to_first_playout_us="<<firstUs<<" real_worker=true fake_device=true\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
