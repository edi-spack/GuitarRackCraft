// Exercise RackCraft's mono input + stereo fixed-block output contract with a
// simulated USB controller. Does not establish Android permissions or DSP cost.
#include <cstring>
#include <oboe/Oboe.h>
#include <oboe_directusb.h>
#include <atomic>
#include <chrono>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace oboe;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
struct Rack : AudioStreamDataCallback {
    AudioStream* input=nullptr;
    std::vector<float> mono;
    std::atomic<unsigned> calls{0};
    std::atomic<bool> signal{false},bad{false};
    explicit Rack(unsigned frames):mono(frames) {}
    DataCallbackResult onAudioReady(AudioStream*,void* data,int32_t frames) override {
        if(frames!=int(mono.size())) {bad=true;return DataCallbackResult::Stop;}
        auto got=input->read(mono.data(),frames,0);
        int available=got?got.value():0;
        auto* out=static_cast<float*>(data);
        for(int f=0;f<frames;++f) {
            float sample=f<available?mono[f]:0;
            out[2*f]=out[2*f+1]=sample*0.5f; // deterministic effect, mono -> stereo
            if(sample!=0) signal=true;
        }
        ++calls;return DataCallbackResult::Continue;
    }
};
int main() {
    try {
        auto mock=dlopen("libdirectusb.so",RTLD_NOW);check(mock,"load controller mock");
        auto high=reinterpret_cast<void(*)(int)>(dlsym(mock,"mock_high_rates"));
        auto active=reinterpret_cast<int(*)()>(dlsym(mock,"mock_active"));
        check(high && active,"mock API");high(1);
        for(int rate : {48000,96000,192000}) for(int fixed : {64,256,1024}) {
            int fd=open("/dev/null",O_RDWR);unsigned char descriptors[18]{18,1};
            check(!oboe_directusb_attach_device(fd,descriptors,sizeof(descriptors)),"bootstrap");close(fd);
            AudioStreamBuilder in,out;
            in.setDirection(Direction::Input)->setAudioApi(AudioApi::AAudio)->setFormat(AudioFormat::Float)->setChannelCount(1)->setSampleRate(rate)->setSampleRateConversionQuality(SampleRateConversionQuality::None);
            std::shared_ptr<AudioStream> input,output;
            check(in.openStream(input)==Result::OK,"input first");Rack rack(fixed);rack.input=input.get();
            out.setDirection(Direction::Output)->setAudioApi(AudioApi::AAudio)->setFormat(AudioFormat::Float)->setChannelCount(2)->setSampleRate(rate)->setDataCallback(&rack);
            check(out.openStream(output)==Result::OK,"probe output");output->close();output.reset();
            out.setFramesPerDataCallback(fixed);check(out.openStream(output)==Result::OK,"reopen fixed block");
            check(active()==1 && output->getHardwareSampleRate()==rate,"one native duplex session");
            input->requestStart();output->requestStart();
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while((rack.calls<5 || !rack.signal) && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check(rack.calls>=5 && rack.signal && !rack.bad,"fixed callback processes captured signal");
            output->stop();output->close();output.reset(); // quiesce before deleting input
            input->stop();input->close();input.reset();check(active()==0,"releases shared USB ownership");
        }
        std::cout<<"PASS RackCraft duplex contract: three rates, three DSP block sizes, output reopen and ordered teardown\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}
