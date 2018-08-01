//
// Created by zhoushengmeng on 18-7-31.
//

class GpuReader{
public:
    virtual int getGpuFreq(){};
};
int readFreq(char* path){
    char buf[128];
    fstream s(path, s.in);
    if (!s.is_open()) {
        std::cout << "failed to open " << path << '\n';
    } else {
        s.read(buf,128);
        s.close();
    }
    int freq = atoi(buf);
    return freq;
}
class QcomGpuReader:public GpuReader{
private:
    string gpuPath = "/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq";
public:
    int getGpuFreq(){
        return readFreq((char*)gpuPath.c_str());
    }
};

#include <regex>
class MtkGpuReader:public GpuReader{
private:
    string gpuPath = "/proc/gpufreq/gpufreq_var_dump";
public:
    int getGpuFreq(){
        int freq=0;
        int fdr = open(gpuPath.c_str(), O_RDONLY);
        if (fdr >= 0) {
            io::file_descriptor_source fdDevice(fdr, io::file_descriptor_flags::close_handle);
            io::stream <io::file_descriptor_source> in(fdDevice);
            if (fdDevice.is_open()) {
                std::string line;
                while (std::getline(in, line)) {
                    // using printf() in all tests for consistency
                    std::size_t found  = line.find("g_cur_gpu_freq");
                    if (found!=std::string::npos){
                        //std::cout << "first 'g_cur_gpu_freq' found at: " << found << '\n';
                    }else{
                        found  = line.find("g_cur_opp_freq");
                        if (found!=std::string::npos){
                            //std::cout << "first 'g_cur_opp_freq' found at: " << found << '\n';
                        }
                    }

                    if(found!=std::string::npos){
                        //printf("%s\n", line.c_str());
                        regex e("(.*?g_cur_opp_freq\\s*=\\s*|g_cur_gpu_freq\\s*=\\s*)(\\d+)(.*)");
                        smatch sm;
                        regex_match(line, sm, e);
//                        cout<<"start to match "<<sm.size()<<endl;
//                        for (unsigned i=0; i<sm.size(); ++i)
//                        {
//                            std::cout << "[" << sm[i] << "] ";
//                        }
//                        cout<<endl;
                        if(sm.size()>=4){
                            freq = atoi(sm[2].str().data());
                        }
                        break;
                    }
                }
                fdDevice.close();
            }
        }
        return freq;
    }
};

#include <boost/filesystem.hpp>
class SamsungGpuReader:public GpuReader{
private:
    string gpuPath = "/sys/bus/platform/devices/";
public:
    SamsungGpuReader(){
        string tmp = "";
        for(boost::filesystem::directory_iterator iter(this->gpuPath);iter!=boost::filesystem::directory_iterator();++iter){
            string path = iter->path().filename().c_str();
            //cout<<path<<endl;
            if( path.find(".mali") == path.size()-5){
                tmp = path;
            }
        }
        this->gpuPath += tmp;
        this->gpuPath += "/clock";
        cout<<"gpu:"<<this->gpuPath<<endl;
    }
    int getGpuFreq(){

        return readFreq((char*)this->gpuPath.c_str());
    }
};

GpuReader* gpuReader;