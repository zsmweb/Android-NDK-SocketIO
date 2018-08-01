//
//  sio_test_sample.cpp
//
//  Created by Melo Yao on 3/24/15.
//

#include "sio_client.h"
#include <sys/inotify.h>
#include <functional>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#ifdef WIN32
#define HIGHLIGHT(__O__) std::cout<<__O__<<std::endl
#define EM(__O__) std::cout<<__O__<<std::endl

#include <stdio.h>
#include <tchar.h>
#define MAIN_FUNC int _tmain(int argc, _TCHAR* argv[])
#else
#define HIGHLIGHT(__O__) std::cout<<"\e[1;31m"<<__O__<<"\e[0m"<<std::endl
#define EM(__O__) std::cout<<"\e[1;30;1m"<<__O__<<"\e[0m"<<std::endl

#define MAIN_FUNC int main(int argc ,const char* args[])
#endif

using namespace sio;
using namespace std;
std::mutex _lock;
std::condition_variable_any _cond;
bool connect_finish = false;
socket::ptr current_socket;

char*  watchfd_to_name[512]={0};
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <fcntl.h>

namespace io = boost::iostreams;
unsigned num_cpus=0;
map<int,string> clustermap;

static string readLineByLineBoost(char* filename) {
    string ret = "";
    int fdr = open(filename, O_RDONLY);
    if (fdr >= 0) {
        io::file_descriptor_source fdDevice(fdr, io::file_descriptor_flags::close_handle);
        io::stream <io::file_descriptor_source> in(fdDevice);
        if (fdDevice.is_open()) {
            std::string line;
            while (std::getline(in, line)) {
                // using printf() in all tests for consistency
                //printf("%s\n", line.c_str());
                ret += line.c_str();
                ret += "\\n";
            }
            fdDevice.close();
        }
    }
    cout<<"all is:"<<endl<<ret<<endl;
    return ret;
}

#include <unistd.h>
#include <map>
#define MAX_EVENTS 1024 /*Max. number of events to process at one go*/
#define LEN_NAME 16 /*Assuming that the length of the filename won't exceed 16 bytes*/
#define EVENT_SIZE  ( sizeof (struct inotify_event) ) /*size of one event*/
#define BUF_LEN     ( MAX_EVENTS * ( EVENT_SIZE + LEN_NAME )) /*buffer to store the data of events*/
class fileWatcher{
private:
    int fd;
    map<int,int> map_;
    pthread_t tid;
    static fileWatcher* instance_;
    fileWatcher(){
        this->fd = inotify_init();
        cout<<"the fd is :"<<this->fd<<":"<<endl;
        if ( this->fd < 0 ) {
            HIGHLIGHT("Couldn't initialize inotify");
        }
        pthread_create(&this->tid,NULL,fileWatcher::ThreadLoop,this);
    }
public:
    socket::ptr* current_socket;
    bool add2watch(string filename){
        int wd = inotify_add_watch(fd, filename.c_str(), IN_CREATE | IN_MODIFY | IN_DELETE);
        watchfd_to_name[wd] = realpath(filename.c_str(),NULL);
        if (wd == -1){
            HIGHLIGHT("Couldn't add watch to");
            HIGHLIGHT(filename.c_str());
            return false;
        }else{
            HIGHLIGHT("Watching:: ");
            HIGHLIGHT(filename.c_str());
            this->map_.insert(pair<int,int>(wd,fd));
            return true;
        }
    }

    ~fileWatcher(){
        map<int, int>::iterator iter;
        for (iter=this->map_.begin(); iter!=this->map_.end(); iter++){
            inotify_rm_watch(iter->first,iter->second);
        }
        close(this->fd);
    }

    static fileWatcher* getInstance(socket::ptr* sock){
        if(fileWatcher::instance_ == NULL){
            fileWatcher::instance_ = new fileWatcher();
        }
        instance_->current_socket = sock;
        return fileWatcher::instance_;
    }

    string             /* Display information from inotify_event structure */
    displayInotifyEvent(struct inotify_event *i)
    {
        printf("    wd =%2d; ", i->wd);
        if (i->cookie > 0)
            printf("cookie =%4d; ", i->cookie);

        printf("mask = ");
        if (i->mask & IN_ACCESS)        printf("IN_ACCESS ");
        if (i->mask & IN_ATTRIB)        printf("IN_ATTRIB ");
        if (i->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE ");
        if (i->mask & IN_CLOSE_WRITE)   printf("IN_CLOSE_WRITE ");
        if (i->mask & IN_CREATE)        printf("IN_CREATE ");
        if (i->mask & IN_DELETE)        printf("IN_DELETE ");
        if (i->mask & IN_DELETE_SELF)   printf("IN_DELETE_SELF ");
        if (i->mask & IN_IGNORED)       printf("IN_IGNORED ");
        if (i->mask & IN_ISDIR)         printf("IN_ISDIR ");
        if (i->mask & IN_MODIFY)        printf("IN_MODIFY ");
        if (i->mask & IN_MOVE_SELF)     printf("IN_MOVE_SELF ");
        if (i->mask & IN_MOVED_FROM)    printf("IN_MOVED_FROM ");
        if (i->mask & IN_MOVED_TO)      printf("IN_MOVED_TO ");
        if (i->mask & IN_OPEN)          printf("IN_OPEN ");
        if (i->mask & IN_Q_OVERFLOW)    printf("IN_Q_OVERFLOW ");
        if (i->mask & IN_UNMOUNT)       printf("IN_UNMOUNT ");
        printf("\n");

        if (i->len > 0)
            printf("        name = %s\n", i->name);

        string data = readLineByLineBoost(watchfd_to_name[i->wd]);
        string json = "{\"path\":\"";
        json += watchfd_to_name[i->wd];
        json += "\",\"data\":\"";
        json += data;
        json += "\"}";
        return json;
    }

    static void *ThreadLoop(void* arg){
        HIGHLIGHT("thread loop start");
        fileWatcher* fw = (fileWatcher*)arg;
        int length, i = 0;
        char buffer[BUF_LEN];
        while (true){
            HIGHLIGHT("thread loop ...");
            //cout<<"the fd is :"<<fw->fd<<":"<<endl;
            length = read( fw->fd, buffer, BUF_LEN );
            cout<<"the data length is "<<length<<endl;
            if ( length < 0 ) {
                HIGHLIGHT( "read" );
            }
            while ( i < length ) {
                struct inotify_event *event = (struct inotify_event *) &buffer[i];
                string json = fw->displayInotifyEvent(event);
                (*fw->current_socket)->emit("data",json);
                i += EVENT_SIZE;
            }
            i=0;
        }
    }

};
fileWatcher* fileWatcher::instance_ = NULL;

class connection_listener
{
    sio::client &handler;

public:
    
    connection_listener(sio::client& h):
    handler(h)
    {
    }
    

    void on_connected()
    {
        _lock.lock();
        _cond.notify_all();
        connect_finish = true;
        _lock.unlock();
    }
    void on_close(client::close_reason const& reason)
    {
        std::cout<<"sio closed "<<std::endl;
        exit(0);
    }
    
    void on_fail()
    {
        std::cout<<"sio failed "<<std::endl;
        exit(0);
    }
};

int participants = -1;



void bind_events()
{
	current_socket->on("new message", sio::socket::event_listener_aux([&](string const& name, message::ptr const& data, bool isAck,message::list &ack_resp)
                       {
                           _lock.lock();
                           string user = data->get_map()["username"]->get_string();
                           string message = data->get_map()["message"]->get_string();
                           EM(user<<":"<<message);
                           _lock.unlock();
                       }));
    
    current_socket->on("user joined",sio::socket::event_listener_aux([&](string const& name, message::ptr const& data, bool isAck,message::list &ack_resp)
                       {
                           _lock.lock();
                           string user = data->get_map()["username"]->get_string();
                           participants  = data->get_map()["numUsers"]->get_int();
                           bool plural = participants !=1;
                           
                           //     abc "
                           HIGHLIGHT(user<<" joined"<<"\nthere"<<(plural?" are ":"'s ")<< participants<<(plural?" participants":" participant"));
                           _lock.unlock();
                       }));
    current_socket->on("user left", sio::socket::event_listener_aux([&](string const& name, message::ptr const& data, bool isAck,message::list &ack_resp)
                       {
                           _lock.lock();
                           string user = data->get_map()["username"]->get_string();
                           participants  = data->get_map()["numUsers"]->get_int();
                           bool plural = participants !=1;
                           HIGHLIGHT(user<<" left"<<"\nthere"<<(plural?" are ":"'s ")<< participants<<(plural?" participants":" participant"));
                           _lock.unlock();
                       }));
}


#include <sys/time.h>
#include <sys/system_properties.h>
#include <sstream>
#include <fstream>

static float getFps(){
    char buf[512];
    char* path = "/data/local/tmp/fps";
    fstream s(path, s.in);
    if (!s.is_open()) {
        std::cout << "failed to open " << path << '\n';
    } else {
        s.read(buf,512);
        s.close();
    }
    int i=0;
    for(;i<strlen(buf);i++){
        if(buf[i] == ':'){
            break;
        }
    }
    i++;
    //cout<<"index:"<<i<<":"<<buf+i<<endl;
    float fps = atof(buf+i);
    //cout<<"fps is :"<<fps<<endl;
    return fps;
}


#include "jsondata.cpp"

static void *doJsonData(void* arg){
    while(true){
        unsigned long int sec= time(NULL);
        //cout<<":"<<sec*1000<<endl;
        float fps = getFps();

        ostringstream json;

        json << "{\"time\":";
        json << sec*1000;
        json << ",\"fps\":\"";
        json << fps;
        json << "\",\"gpufreq\":\"";
        json << gpuReader->getGpuFreq();
        json << "\",\"cputemp\":\"";
        json << readFreq("/sys/class/meizu/cpu_temp/temp");
        json << "\",\"pcbtemp\":\"";
        json << readFreq("/sys/class/meizu/pcb_temp/temp");
        json << "\",\"cpus\":[";
        map<int, string>::iterator iter;
        int idx=0;
        for(iter = clustermap.begin(); iter != clustermap.end(); ){
            json << "{\"idx\":\"";
            json << idx;
            json << "\",\"freq\":\"";
            json << readFreq((char*)iter->second.c_str());
            iter++;
            if(iter == clustermap.end()){
                json << "\"}]";
            }else{
                json << "\"},";
            }

            idx++;
        }
        json << "}";

        //cout<<"|"<< json.str() <<"|"<<endl;

        current_socket->emit("json_data",json.str());
        sleep(1);
    }
}




static int writeFile(string path,string data){
    std::fstream s(path, s.binary | s.trunc | s.in | s.out);
    if (!s.is_open()) {
        std::cout << "failed to open " << path << '\n';
    } else {
        s.write(data.c_str(),data.size());
        s.flush();
        s.close();
    }
    return 0;
}

#include <boost/algorithm/string.hpp>

MAIN_FUNC
{
    string url = "http://127.0.0.1:3000";
    if(argc >= 2){
        string ip = readLineByLineBoost("/data/local/tmp/ipaddr");
        if(ip.length()>0){
            url = "http://";
            url += boost::algorithm::replace_all_copy(ip,"\\n","");
            url += ":3000";
            cout<<"use remote ip:"<<url<<endl;
        }
    }

    {
        num_cpus = std::thread::hardware_concurrency();
        std::cout << "Launching " << num_cpus << " threads\n";
        char cpuMaxpath[PATH_MAX];
        char cpuCurpath[PATH_MAX];
        for(unsigned i=0;i<num_cpus;i++){
            sprintf(cpuMaxpath,"/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq",i);
            sprintf(cpuCurpath,"/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq",i);
            int freq = readFreq(cpuMaxpath);
            if(clustermap.find(freq) == clustermap.end()){
                clustermap.insert(pair<int,string>(freq,cpuCurpath));
                cout<<"freq:"<<freq<<" path:"<<cpuCurpath<<endl;
            }
        }

        char soc[PROP_VALUE_MAX +1] = "qcom";
        __system_property_get("ro.soc.vendor", soc);
        if(strlen(soc)==0){
            __system_property_get("debug.soc", soc);
        }
        string socs = soc;
        if(socs == "qcom"){
            gpuReader = new QcomGpuReader();
        } else if(socs == "MTK"){
            gpuReader = new MtkGpuReader();
        } else{
            gpuReader = new SamsungGpuReader();
        }
    }

    sio::client h;
    connection_listener l(h);

    fileWatcher* fw = fileWatcher::getInstance(&current_socket);
    
    h.set_open_listener(std::bind(&connection_listener::on_connected, &l));
    h.set_close_listener(std::bind(&connection_listener::on_close, &l,std::placeholders::_1));
    h.set_fail_listener(std::bind(&connection_listener::on_fail, &l));
    h.connect(url);
    _lock.lock();
    if(!connect_finish)
    {
        _cond.wait(_lock);
    }
    _lock.unlock();
	current_socket = h.socket();
Login:
    string nickname = "client";
//    while (nickname.length() == 0) {
//        HIGHLIGHT("Type your nickname:");
//
//        getline(cin, nickname);
//
//    }
	current_socket->on("login", sio::socket::event_listener_aux([&](string const& name, message::ptr const& data, bool isAck,message::list &ack_resp){
        _lock.lock();
        participants = data->get_map()["numUsers"]->get_int();
        bool plural = participants !=1;
        HIGHLIGHT("Welcome to Socket.IO Chat-\nthere"<<(plural?" are ":"'s ")<< participants<<(plural?" participants":" participant"));
        _cond.notify_all();
        _lock.unlock();
        current_socket->off("login");
    }));

    current_socket->on("getfile", sio::socket::event_listener_aux([&](string const& name, message::ptr const& data, bool isAck,message::list &ack_resp){
        _lock.lock();
        string path = data->get_map()["path"]->get_string();

        string data1 = readLineByLineBoost((char*)path.c_str());
        string json = "{\"path\":\"";
        json += path;
        json += "\",\"data\":\"";
        json += data1;
        json += "\"}";
        current_socket->emit("data",json);

        HIGHLIGHT(path);
        fw->add2watch(path);
        _cond.notify_all();
        _lock.unlock();
        current_socket->off("login");
    }));

    current_socket->on("cmd", sio::socket::event_listener_aux([&](string const& name, message::ptr const& data, bool isAck,message::list &ack_resp){
        _lock.lock();
        string cmd = data->get_map()["cmd"]->get_string();
        if(cmd == "srs"){
            string path = data->get_map()["path"]->get_string();
            vector<shared_ptr<sio::message>> srs_cfg = data->get_map()["srs_cfg"]->get_vector();
            int size = srs_cfg.size();
            cout<<"size is:"<<size<<endl;

            ostringstream cmdline;
            if(size>=4){
                cmdline<<srs_cfg.at(0)->get_int();
                cmdline<<"  ";
                cmdline<<srs_cfg.at(1)->get_int();
                cmdline<<"  ";
                cmdline<<srs_cfg.at(2)->get_int();
                cmdline<<"  ";
                cmdline<<srs_cfg.at(3)->get_int();
            }
            cout << path <<" "<< cmdline.str()<<endl;
            writeFile(path,cmdline.str());
        }

        _cond.notify_all();
        _lock.unlock();
        current_socket->off("login");
    }));

    current_socket->emit("add user", nickname);

    string clientinfo = "{\"type\":\"phone\",\"sn\":\"";
    char sn[PROP_VALUE_MAX +1] = "0000000000";
    __system_property_get("ro.serialno", sn);
    clientinfo = clientinfo + sn;
    clientinfo = clientinfo + "\"}";
    cout<<clientinfo<<endl;
    current_socket->emit("clientinfo", clientinfo);

    pthread_t tid;
    pthread_create(&tid, NULL, doJsonData,NULL);

    _lock.lock();
    if (participants<0) {
        _cond.wait(_lock);
    }
    _lock.unlock();
    bind_events();
    
    HIGHLIGHT("Start to chat,commands:\n'$exit' : exit chat\n'$nsp <namespace>' : change namespace");
    for (std::string line; std::getline(std::cin, line);) {
        if(line.length()>0)
        {
            if(line == "$exit")
            {
                break;
            }
            else if(line.length() > 5&&line.substr(0,5) == "$nsp ")
            {
                string new_nsp = line.substr(5);
                if(new_nsp == current_socket->get_namespace())
                {
                    continue;
                }
                current_socket->off_all();
                current_socket->off_error();
                //per socket.io, default nsp should never been closed.
                if(current_socket->get_namespace() != "/")
                {
                    current_socket->close();
                }
                current_socket = h.socket(new_nsp);
                bind_events();
                //if change to default nsp, we do not need to login again (since it is not closed).
                if(current_socket->get_namespace() == "/")
                {
                    continue;
                }
                goto Login;
            }
            current_socket->emit("new message", line);
            _lock.lock();
            EM("\t\t\t"<<line<<":"<<"You");
            _lock.unlock();
        }
    }
    HIGHLIGHT("Closing...");
    h.sync_close();
    h.clear_con_listeners();
	return 0;
}

