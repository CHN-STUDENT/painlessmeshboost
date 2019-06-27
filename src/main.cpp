
#include <algorithm>
#include <boost/algorithm/hex.hpp>
#include <boost/filesystem.hpp>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <iterator>

#include "Arduino.h"

WiFiClass WiFi;
ESPClass ESP;
#include "fcgio.h"
const unsigned long STDIN_MAX = 1000000;

#include "painlessMesh.h"
#include "painlessMeshConnection.h"

painlessmesh::logger::LogClass Log;

#undef F
#include <boost/date_time.hpp>
#include <boost/program_options.hpp>
#define F(string_literal) string_literal
namespace po = boost::program_options;

#include <iostream>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <vector>

#define OTA_PART_SIZE 1024
#include "ota.hpp"
using namespace std;

template <class T>
// bool contains(T &v, T::value_type const value) {
bool contains(T& v, std::string const value) {
  return std::find(v.begin(), v.end(), value) != v.end();
}

std::string timeToString() {
  boost::posix_time::ptime timeLocal =
      boost::posix_time::second_clock::local_time();
  return to_iso_extended_string(timeLocal);
}

// Will be used to obtain a seed for the random number engine
static std::random_device rd;
static std::mt19937 gen(rd());

uint32_t runif(uint32_t from, uint32_t to) {
  std::uniform_int_distribution<uint32_t> distribution(from, to);
  return distribution(gen);
}


/**
 * Note this is not thread safe due to the static allocation of the
 * content_buffer.
 */
string get_request_content(const FCGX_Request & request) {
    char * content_length_str = FCGX_GetParam("CONTENT_LENGTH", request.envp);
    unsigned long content_length = STDIN_MAX;

    if (content_length_str) {
        content_length = strtol(content_length_str, &content_length_str, 10);
        if (*content_length_str) {
            cerr << "Can't Parse 'CONTENT_LENGTH='"
                 << FCGX_GetParam("CONTENT_LENGTH", request.envp)
                 << "'. Consuming stdin up to " << STDIN_MAX << endl;
        }

        if (content_length > STDIN_MAX) {
            content_length = STDIN_MAX;
        }
    } else {
        // Do not read from stdin if CONTENT_LENGTH is missing
        content_length = 0;
    }

    char * content_buffer = new char[content_length];
    cin.read(content_buffer, content_length);
    content_length = cin.gcount();

    // Chew up any remaining stdin - this shouldn't be necessary
    // but is because mod_fastcgi doesn't handle it correctly.

    // ignore() doesn't set the eof bit in some versions of glibc++
    // so use gcount() instead of eof()...
    do cin.ignore(1024); while (cin.gcount() == 1024);

    string content(content_buffer, content_length);
    delete [] content_buffer;
    return content;
}


void split_string(const std::string& s, std::vector<std::string>& v, const std::string& c)
{
  //https://www.zhihu.com/question/36642771
  std::string::size_type pos1, pos2;
  pos2 = s.find(c);
  pos1 = 0;
  while(std::string::npos != pos2)
  {
    v.push_back(s.substr(pos1, pos2-pos1));

    pos1 = pos2 + c.size();
    pos2 = s.find(c, pos1);
  }
  if(pos1 != s.length())
    v.push_back(s.substr(pos1));
}

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
  //https://stackoverflow.com/questions/3418231/replace-part-of-a-string-with-another-string
  if(from.empty())
      return;
  size_t start_pos = 0;
  while((start_pos = str.find(from, start_pos)) != std::string::npos) {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
  }
}

const unsigned NODE_NUM_MAX = 10;

struct nodeVector
{
    uint32_t nodeTime;
    std::string printTime;
    std::string sensor;
    std::string message;
    nodeVector() 
    {
      nodeTime=0;
      printTime="";
      sensor="";
      message="";
    }
};

struct nodeInfo
{
    std::string id;
    bool isConnect;
    vector <nodeVector> v;
    nodeInfo () 
    {
        id = "";
        isConnect = false;
    }
};

struct nodeInfo n[NODE_NUM_MAX];
std::string nodeTree="";
int nodeNum=0;

int main(void) {
 
  try {

    int ac = 4;
    char *av[ac+1];

    av[0] = "painlessMeshBoost";
    av[1] = "-n";
    av[2] = "1111111111";
    av[3] = "-s";
    av[4] = "-c";
    //av[5] = "192.168.4.1";
    av[5] = "\0";

    streambuf * cin_streambuf  = cin.rdbuf();
    streambuf * cout_streambuf = cout.rdbuf();
    streambuf * cerr_streambuf = cerr.rdbuf();


    FCGX_Request request;
   
    

    FCGX_Init();
    FCGX_InitRequest(&request, 0, 0);

    size_t port = 5555;
    std::string ip = "192.168.4.1";
    std::vector<std::string> logLevel;
    size_t nodeId = runif(0, std::numeric_limits<uint32_t>::max());
    std::string otaDir;

    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "Produce this help message")(
        "nodeid,n", po::value<size_t>(&nodeId),
        "Set nodeID, otherwise set to a random value")(
        "port,p", po::value<size_t>(&port), "The mesh port (default is 5555)")(
        "server,s",
        "Listen to incoming node connections. This is the default, unless "
        "--client "
        "is specified. Specify both if you want to both listen for incoming "
        "connections and try to connect to a specific node.")(
        "client,c", po::value<std::string>(&ip),
        "Connect to another node as a client. You need to provide the ip "
        "address of the node.")(
        "log,l", po::value<std::vector<std::string>>(&logLevel),
        "Only log given events to the console. By default all events are logged, this allows you to filter which ones to log. Events currently logged are: receive, connect, disconnect, change, offset and delay. This option can be specified multiple times to log multiple types of events.")(
        "ota-dir,d", po::value<std::string>(&otaDir),
        "Watch given folder for new firmware files.");

    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 0;
    }

    Scheduler scheduler;
    boost::asio::io_service io_service;
    painlessMesh mesh;
    Log.setLogLevel(ERROR);
    mesh.init(&scheduler, nodeId, port);
    std::shared_ptr<AsyncServer> pServer;
    if (vm.count("server") || !vm.count("client")) {
      pServer = std::make_shared<AsyncServer>(io_service, port);
      painlessmesh::tcp::initServer<MeshConnection, painlessMesh>(*pServer,
                                                                  mesh);
    }

    std::shared_ptr<AsyncClient> pClient;
    if (vm.count("client")) {
      pClient = std::make_shared<AsyncClient>(io_service);
      painlessmesh::tcp::connect<MeshConnection, painlessMesh>(
          (*pClient), boost::asio::ip::address::from_string(ip), port, mesh);
    }

    if (logLevel.size() == 0 || contains(logLevel, "receive")) {
      mesh.onReceive([&mesh](uint32_t nodeId, std::string& msg) {
        //ofstream outfile;
        //outfile.open("/root/painlessmeshboost/log.txt", ios::app|ios::out);
        // outfile << "{\"event\":\"receive\",\"nodeTime\":"
        //           << mesh.getNodeTime() << ",\"time\":\"" << timeToString()
        //           << "\""
        //           << ",\"nodeId\":" << nodeId << ",\"msg\":\"" << msg << "\"}"
        //           << std::endl;
        vector <std::string> v;
        split_string(msg,v,"/");
        struct nodeVector temp;
        temp.sensor = v[0];
        temp.message = v[1];
        temp.nodeTime = mesh.getNodeTime();
        temp.printTime = timeToString();
        //outfile <<"sensor"<< temp.sensor << std::endl;
        //outfile <<"msg"<< temp.message << std::endl;
        v.clear();
        bool f = false;
        char tempId[20];
         //uint32_t to string 
        std::string id;
        sprintf( tempId, "%u", nodeId );
        id = tempId;
        //outfile << "id" <<id<<std::endl;
        //outfile.close();
        for(int i=0;i<NODE_NUM_MAX;i++)
        {
          if(n[i].id==id)
          { //push message info message quene
            n[i].isConnect = true;
            if(n[i].v.empty())
            { //Sensor message is null
              n[i].v.push_back(temp);
              nodeNum=i+1;
              f = true;
              break;
            }
            else
            {
              //Sensor message need to update 
              bool flag = false;
              for(int j=0;j<n[i].v.size();j++)
              { 
                if(n[i].v[j].sensor == temp.sensor)
                {
                  n[i].v[j].message =  temp.message;
                  n[i].v[j].nodeTime =  temp.nodeTime;
                  n[i].v[j].printTime =  temp.printTime;
                  f = true;
                  flag = true;
                  nodeNum=i+1;
                  break;
                }
              }
              if(flag == false)
              {//Sensor message is null
                n[i].v.push_back(temp);
                f = true;
                nodeNum=i+1;
                break;
              }
            }
          }
        }
        if(f==false)
        {
          if(nodeNum > NODE_NUM_MAX) 
          {
            std::cerr << "error: Can not connect more node"<< std::endl;
          }
          else
          {
            n[nodeNum].id = id;
            n[nodeNum].isConnect = true;
            n[nodeNum].v.push_back(temp);
          }
        }
      });
    }
    if (logLevel.size() == 0 || contains(logLevel, "connect")) {
      mesh.onNewConnection([&mesh](uint32_t nodeId) {
        std::cout << "{\"event\":\"connect\",\"nodeTime\":"
                  << mesh.getNodeTime() << ",\"time\":\"" << timeToString()
                  << "\""
                  << ",\"nodeId\":" << nodeId
                  << ", \"layout\":" << mesh.asNodeTree().toString() << "}"
                  << std::endl;
        bool flag = false;
        char tempId[20];
        std::string id;
        sprintf( tempId, "%u", nodeId );
        id = tempId;
        for(int i=0;i<NODE_NUM_MAX;i++)
        {
          //uint32_t to string
          if(n[i].id!="")
          {
            if(n[i].id==id)
            { 
                n[i].isConnect = true;
                flag = true;
                i++;
                nodeNum = i;
                break;
            }
            i++;
          }
        }
        if(flag==false)
        {
          if(nodeNum > NODE_NUM_MAX) 
          {
            std::cerr << "error: Can not connect more node"<< std::endl;
          }
          else
          {
            n[nodeNum].id = id;
            n[nodeNum].isConnect = true;
          }
        }
      });
    }

    if (logLevel.size() == 0 || contains(logLevel, "disconnect")) {
      mesh.onDroppedConnection([&mesh](uint32_t nodeId) {
        // std::cout << "{\"event\":\"disconnect\",\"nodeTime\":"
        //           << mesh.getNodeTime() << ",\"time\":\"" << timeToString()
        //           << "\""
        //           << ",\"nodeId\":" << nodeId
        //           << ", \"layout\":" << mesh.asNodeTree().toString() << "}"
        //           << std::endl;
        for(int i=0;i<NODE_NUM_MAX;i++)
        {
            //uint32_t to string 
            char tempId[20];
            std::string id;
            sprintf( tempId, "%u", nodeId );
            id = tempId;
            if(n[i].id==id)
            { 
                n[i].isConnect = false;
                while(!n[i].v.empty()) {
                    n[i].v.clear();
                }
            }
        }
      });
    }

    if (logLevel.size() == 0 || contains(logLevel, "change")) {
      mesh.onChangedConnections([&mesh]() {
        // std::cout << "{\"event\":\"change\",\"nodeTime\":" << mesh.getNodeTime()
        //           << ",\"time\":\"" << timeToString() << "\""
        //           << ", \"layout\":" << mesh.asNodeTree().toString() << "}"
        //           << std::endl;
        nodeTree = mesh.asNodeTree().toString();
      });
    }

    if (logLevel.size() == 0 || contains(logLevel, "offset")) {
      mesh.onNodeTimeAdjusted([&mesh](int32_t offset) {
      //   std::cout << "{\"event\":\"offset\",\"nodeTime\":" << mesh.getNodeTime()
      //             << ",\"time\":\"" << timeToString() << "\""
      //             << ",\"offset\":" << offset << "}" << std::endl;
      });
    }

    if (logLevel.size() == 0 || contains(logLevel, "delay")) {
      mesh.onNodeDelayReceived([&mesh](uint32_t nodeId, int32_t delay) {
        // std::cout << "{\"event\":\"delay\",\"nodeTime\":" << mesh.getNodeTime()
        //           << ",\"time\":\"" << timeToString() << "\""
        //           << ",\"nodeId\":" << nodeId << ",\"delay\":" << delay << "}"
        //           << std::endl;
      });
    }

    if (vm.count("ota-dir")) {
      using namespace painlessmesh;
      using namespace painlessmesh::plugin;
      // We probably want to temporary store the file
      // md5 -> data
      auto files = std::make_shared<std::map<std::string, std::string>>();
      // Setup task that monitors the folder for changes
      auto task = mesh.addTask(
          scheduler, TASK_SECOND, TASK_FOREVER,
          [files, &mesh, &scheduler, otaDir]() {
            // TODO: Scan for change
            boost::filesystem::path p(otaDir);
            boost::filesystem::directory_iterator end_itr;
            for (boost::filesystem::directory_iterator itr(p); itr != end_itr;
                 ++itr) {
              if (!boost::filesystem::is_regular_file(itr->path())) {
                continue;
              }
              auto stat = addFile(files, itr->path(), TASK_SECOND);
              if (stat.newFile) {
                // When change, announce it, load it into files
                ota::Announce announce;
                announce.md5 = stat.md5;
                announce.role = stat.role;
                announce.hardware = stat.hw;
                announce.noPart =
                    ceil(((float)files->operator[](stat.md5).length()) /
                         OTA_PART_SIZE);
                announce.from = mesh.nodeId;

                auto announceTask =
                    mesh.addTask(scheduler, TASK_MINUTE, 60,
                                 [&mesh, &scheduler, announce]() {
                                   mesh.sendPackage(&announce);
                                 });
                // after anounce, remove file from memory
                announceTask->setOnDisable(
                    [files, md5 = stat.md5]() { files->erase(md5); });
              }
            }
          });
      // Setup reply to data requests
      mesh.onPackage(11, [files, &mesh](protocol::Variant variant) {
        auto pkg = variant.to<ota::DataRequest>();
        // cut up the data and send it
        if (files->count(pkg.md5)) {
          auto reply =
              ota::Data::replyTo(pkg,
                                 files->operator[](pkg.md5).substr(
                                     OTA_PART_SIZE * pkg.partNo, OTA_PART_SIZE),
                                 pkg.partNo);
          mesh.sendPackage(&reply);
        } else {
          Log(ERROR, "File not found");
        }
        return true;
      });
    }
    vector<std::string> v;
    string id="";
    string method="";
    string message="";
    while (true) 
    {
      //usleep(1000);  // Tweak this for acceptable cpu usage
      while (FCGX_Accept_r(&request) == 0) 
      {
        fcgi_streambuf cin_fcgi_streambuf(request.in);
        fcgi_streambuf cout_fcgi_streambuf(request.out);
        fcgi_streambuf cerr_fcgi_streambuf(request.err);  
        cin.rdbuf(&cin_fcgi_streambuf);
        cout.rdbuf(&cout_fcgi_streambuf);
        cerr.rdbuf(&cerr_fcgi_streambuf);
        std::cout << "Content-Type: text/plain\r\n\r\n";
        mesh.update();
        io_service.poll();
        const char * uri = FCGX_GetParam("REQUEST_URI", request.envp);
        string content = get_request_content(request);  
        string u=uri;
        u=u.substr(1);
        split_string(u,v,"/");
        string id;
        // std::cout<<"parameter num "<<v.size()<<std::endl;
        // for(int i=0;i<v.size();i++)
        // {
        //     std::cout<<"parameter " << i << " " + v[i]<<std::endl;
        // }
        if(v.size()==1)
        {
          if(v[0]=="tree") 
          {
            replaceAll(nodeTree,"nodeId","name");
            replaceAll(nodeTree,"subs","children");
            std::cout<<nodeTree<<std::endl;
          }
          else if(v[0].length()==10||v[0].length()==9)
          {
            id=v[0];
            uint32_t dest = strtoul(v[0].c_str(), NULL, 10); //string to uint_32
            if(content!="")
            {
              mesh.sendSingle(dest,content);
            }
            else
            {
              std::cout<<"error:please select a message to send."<<std::endl; 
              std::cout<<"Usage:"<<std::endl;
              std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
              std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
            }
          }
          else
          {
            std::cout<<"error:unknow method."<<std::endl; 
            std::cout<<"Usage:"<<std::endl;
            std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
            std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
          }
        }
        else if(v.size()==3)
        {
          id=v[0];
          int size=id.length();
          //std::cout<<"id size: "<<size<<std::endl;
          if(id.length()!=10&&id.length()!=9)
          {
            std::cout<<"error:node id is wrong."<<std::endl;
            std::cout<<"Usage:"<<std::endl;
            std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
            std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
          }
          else
          {
            if(v[1]=="get") 
            {
              if(v[2]!="")
              {
                bool flag=false;
                for(int i=0;i<NODE_NUM_MAX;i++)
                {
                  // //test start
                  // std::cout<<"Node id"<<n[i].id<<std::endl;
                  // std::cout<<"is Connect "<<n[i].isConnect<<std::endl;
                  // for(int j=0;j<n[i].v.size();j++)
                  // { 
                  //   std::cout<<"sensor"<<n[i].v[j].sensor<<" msg"<<n[i].v[j].message<<std::endl;
                  //   //std::cout<<"sensor"<<v[2]<<std::endl;
                  //   if(n[i].v[j].sensor == v[2])
                  //   {
                  //     std::cout<<n[i].v[j].message<<std::endl;
                  //     //std::cout<<"sensor"<<n[i].v[j].sensor<<" msg"<<n[i].v[j].message<<std::endl;
                  //     flag = true;
                  //     break;
                  //   }
                  // }
                  // //test end
                  if(n[i].id==id)
                  { 
                    if(n[i].isConnect == true) 
                    {
                      if(n[i].v.size() == 0)
                      {
                        std::cout<<"error:this node does not have any message,please try again."<<std::endl;
                        std::cout<<"Usage:"<<std::endl;
                        std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
                        std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
                      }
                      else
                      {
                        bool flag = false;
                        for(int j=0;j<n[i].v.size();j++)
                        { 
                          if(n[i].v[j].sensor == v[2])
                          {
                            std::cout<<n[i].v[j].message<<std::endl;
                            flag = true;
                            break;
                          }
                        }
                        if(flag == false)
                        {//Sensor message is null
                          std::cout<<"error:can not find this node's sensor message."<<std::endl;
                          std::cout<<"Usage:"<<std::endl;
                          std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
                          std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
                        }
                      }
                    }
                    else
                    {
                      std::cout<<"error:node is disconnect to this server."<<std::endl;
                      std::cout<<"Usage:"<<std::endl;
                      std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
                      std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
                    }
                    flag = true;
                  }
                }
                if(flag==false)
                {
                  std::cout<<"error:can not find this node."<<std::endl;
                  std::cout<<"Usage:"<<std::endl;
                  std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
                  std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
                }
              }
              else
              {
                std::cout<<"error:please select a sensor to get the message."<<std::endl; 
                std::cout<<"Usage:"<<std::endl;
                std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
                std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
              }
            }
            else
            {
              std::cout<<"error:unknow method."<<std::endl; 
              std::cout<<"Usage:"<<std::endl;
              std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
              std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
            }
          }
        }
        else if(v.size()==4)
        {
          id=v[0];
          if(id.length()!=10&&id.length()!=9)
          {
            std::cout<<"error:node id is wrong."<<std::endl;
            std::cout<<"Usage:"<<std::endl;
            std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
            std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
          }
          else
          {
            if(v[1]=="send")
            {
              if(v[2]!="")
              {//sensor is not empty
                if(v[3]!="")
                {//msg is not empty
                  uint32_t dest = strtoul(v[0].c_str(), NULL, 10); //string to uint_32
                  string msg=v[2]+"/"+v[3];
                 // std::cout<<"dest"<<dest<<std::endl;
                  mesh.sendSingle(dest,msg);
                }
                else
                {
                  std::cout<<"error:please select a message to send."<<std::endl; 
                  std::cout<<"Usage:"<<std::endl;
                  std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
                  std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
                }
              }
              else
              {
                std::cout<<"error:please select a sensor to send the message."<<std::endl; 
                std::cout<<"Usage:"<<std::endl;
                std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
                std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
              }
            } 
            else
            {
              std::cout<<"error:unknow method."<<std::endl; 
              std::cout<<"Usage:"<<std::endl;
              std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
              std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
            }
          }
        }
        else
        {
          std::cout<<"error:unknow method."<<std::endl; 
          std::cout<<"Usage:"<<std::endl;
          std::cout<<"Get Message:http://ip/id/get/sensor"<<std::endl;
          std::cout<<"Send Message:http://ip/id/send/sensor/msg"<<std::endl;
        }
        v.clear();
          // Note: the fcgi_streambuf destructor will auto flush
      }
    }
    cin.rdbuf(cin_streambuf);
    cout.rdbuf(cout_streambuf);
    cerr.rdbuf(cerr_streambuf);
  } catch (std::exception& e) {
    std::cerr << "error: " << e.what() << std::endl;
    ;
    return 1;
  } catch (...) {
    std::cerr << "Exception of unknown type!" << std::endl;
    ;
  }

  return 0;
}