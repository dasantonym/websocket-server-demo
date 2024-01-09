#include "WebsocketServer.h"

#include <iostream>
#include <thread>
#include <asio/io_service.hpp>
#include "RemoteCaptury.h"

//The port number the WebSocket server listens on
#define PORT_NUMBER 8889

static volatile bool keepRunning = true;

// handle Ctrl+C
static void sigintHandler(int x) {
    keepRunning = false;
}

struct sockaddr_in sout;
int sout_length;
int fd;
char buffer[2048]; // declare a 2Kb buffer for osc messages
char *address;

void newPoseCallback(struct CapturyActor* actor, struct CapturyPose* pose, int trackingQuality) {

}

int main(int argc, char* argv[])
{
	//Create the event loop for the main thread, and the WebSocket server
	asio::io_service mainEventLoop;
	WebsocketServer server;
	
	//Register our network callbacks, ensuring the logic is run on the main thread's event loop
	server.connect([&mainEventLoop, &server](ClientConnection conn)
	{
		mainEventLoop.post([conn, &server]()
		{
			std::clog << "Connection opened." << std::endl;
			std::clog << "There are now " << server.numConnections() << " open connections." << std::endl;
			
			//Send a hello message to the client
			server.sendMessage(conn, "hello", Json::Value());
		});
	});
	server.disconnect([&mainEventLoop, &server](ClientConnection conn)
	{
		mainEventLoop.post([conn, &server]()
		{
			std::clog << "Connection closed." << std::endl;
			std::clog << "There are now " << server.numConnections() << " open connections." << std::endl;
		});
	});
	server.message("message", [&mainEventLoop, &server](ClientConnection conn, const Json::Value& args)
	{
		mainEventLoop.post([conn, args, &server]()
		{
			std::clog << "message handler on the main thread" << std::endl;
			std::clog << "Message payload:" << std::endl;
			for (auto key : args.getMemberNames()) {
				std::clog << "\t" << key << ": " << args[key].asString() << std::endl;
			}
			
			//Echo the message pack to the client
			server.sendMessage(conn, "message", args);
		});
	});
	
	//Start the networking thread
	std::thread serverThread([&server]() {
		server.run(PORT_NUMBER);
	});
	
	//Start a keyboard input thread that reads from stdin
	std::thread inputThread([&server, &mainEventLoop]()
	{
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        fcntl(fd, F_SETFL, O_NONBLOCK); // set the socket to non-blocking

        address = (char*)malloc(32 * sizeof(char));

        char chost[] = "10.200.14.76";
        int cport = 2101;

        if (Captury_connect("10.200.14.76", 2101) == 1 && Captury_synchronizeTime() != 0){
            printf("Successfully connected to Captury at: %s:%i \n", chost, cport);
        }
        else {
            printf("Failed to connect to Captury at: %s:%i \n", chost, cport);
        }

        if (Captury_startStreaming(CAPTURY_STREAM_GLOBAL_POSES) == 1) {
            printf("Successfully started streaming\n");
        }
        else {
            printf("Failed to start streaming\n");
        }

        if (Captury_registerNewPoseCallback(newPoseCallback) == 1){
            printf("Successfully registered poseCallback\n");
        }
        else {
            printf("Failed to register poseCallback\n");
        }


		string input;
		while (1)
		{
            const struct CapturyActor* actors;
            int numActors = Captury_getActors(&actors);

			//Read user input from stdin
			std::getline(std::cin, input);
			
			//Broadcast the input to all connected clients (is sent on the network thread)
			Json::Value payload;
			payload["input"] = input;
			server.broadcastMessage("userInput", payload);
			
			//Debug output on the main thread
			mainEventLoop.post([]() {
				std::clog << "User input debug output on the main thread" << std::endl;
			});
		}
	});
	
	//Start the event loop for the main thread
	asio::io_service::work work(mainEventLoop);
	mainEventLoop.run();
	
	return 0;
}
