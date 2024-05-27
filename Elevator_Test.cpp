/*
# =====================================================================
# Title             : Elevator_Test.cpp
# Description       : This program uses 3 concurrent threads to perform scheduling for an Elevator OS also communicates with API
                    : Runs the Elevator OS, but we are designing the scheduler for the Elevator OS which decides which floor
                    : each elevator goes to.
# Author            : Christian Tran (R11641653) 
# Usage             : For use in HPCC with API from Dr. Rees
# Notes             : requires HPCC, sbatch command, and Dr. Rees help. 5 hour maximum runtime for the simulation to run
                    : The goal of the final project is to visit past assignments: 
                    :  Assignment #2 HPCC and compilation
                    :  Assignment #3 API communication (note: api communication will be driven EXACTLY the same in this assignment)
                    :  Assignment #4 MultiThreading (Require 3 threads, 2 communication threads (input and output thread) and 1 scheduling thread (decision thread))
                    :  Assignment #5 Scheduling Algorithms (Already written scheduling algorithms, just passing in different data)
=====================================================================
*/
#include <iostream>
#include <condition_variable>
#include <math.h>
#include <mutex>
#include <queue>
#include <deque>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <curl/curl.h>

using namespace std; // Import namespace to not have to call std every time.

//Person Object Class
class PersonObj{
  public:
    string pName; //Person name
    int start_floor; //Person's starting floor
    int end_floor; //The floor the person needs to go to
};

//Elevator Object Class
class ElevatorObj{
  public:
    string eName;
    int lFloor;
    int hFloor;
    int cFloor;
    int t_capacity;  
};

//Combo Object Class. Used for output thread
class ComboObj{
  public: 
    string personID; //Name of the person to assign to the elevator
    string elevatorID; //Name of the elevator to pass to the API command
};

static const int max_args = 3; //Maximum number of input arguments allowed from command line 

//Process queue used for scheduler (not shared)
static deque<ElevatorObj> eleDQ; //Deque of the elevators (processes)

//Shared queues
static const int personQ_size = 50; // Max size of the queue (buffer size)
static queue<PersonObj> personQ; // queue to control the producer read data (buffer itself)
static queue<ComboObj> outputQ; //Output personQ

static mutex mtx1; // mutex lock to protect the queue between the producer and consumer
static mutex mtx2; // second mutex lock for the outPersonQ

//Previous condition variables from assignment 4
//condition_variable condV_reader; // condition variable to communicate to decision thread
//condition_variable condV_decision; // condition variable to communicate to reader thread

//Condition variables for the 3 threads
static condition_variable condV_Input; // condition variable to communicate to Input thread
static condition_variable condV_Scheduler; // condition variable to communicate to Scheduler thread
static condition_variable condV_Output; //condition variable to communicate to Output thread

/* Function name: WriteCallback()
   Purpose: From assignment 3, used with the GET curl commands to return a string from API
   Return type: Void
   Input Params: nothing 
*/
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}


/* Function name: printlist()
   Purpose: utility function that prints out the main input deque to check if it is correct
   Return type: Void
   Input Params: nothing 
   Works?: Confirmed Yes 3/30/2024
*/
static void printlist (deque<ElevatorObj> inputDQ){
  for(int i = 0; i < inputDQ.size(); i++){
    cout << inputDQ.at(i).eName << "\t" << inputDQ.at(i).lFloor<< "\t" << inputDQ.at(i).hFloor << "\t" << inputDQ.at(i).cFloor << "\t" << inputDQ.at(i).t_capacity << endl; 
  }
}

/* Function name: add_person_to_ele() (/Simulation/start PUT request to start the simulation)
   Purpose: A function that sends the /Simulation/start command to API
   Return type: Void
   Input Params: nothing 
   Works?: Confirmed on 4/22/2024
*/
static void add_person_to_ele(string input_pID, string input_eID){
  // Initialize libcurl
  curl_global_init(CURL_GLOBAL_ALL);

  // Create a curl handle
  CURL* curl = curl_easy_init();
  if (curl) {

    //Construct URL for the PUT request
    string original_URL = "http://localhost:5432/AddPersonToElevator/";

    //New url creation with arguments in the form: /AddPersonToElevator/personID/elevatorID
    string new_URL = original_URL + input_pID + "/" + input_eID;
    

    
    //Reference fields by name
    curl_easy_setopt(curl, CURLOPT_URL, new_URL.c_str());  //Send Curl PUT request to /Simulation/start command
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); //tells what kind of command this is

    // Perform the PUT request
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) { //Test for error. It shouldn't return an error since its all local host.
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
    }

    // Cleanup the curl handle
    curl_easy_cleanup(curl);
    } //Note: calls both curl_easy_cleanup and curl_global_cleanup to "clean" it from memory so that we can immediately use it again.
    curl_global_cleanup();
}



/* Function name: start_sim() (/Simulation/start PUT request to start the simulation)
   Purpose: A function that sends the /Simulation/start command to API
   Return type: Void
   Input Params: nothing 
   Works?: Confirmed this works 4/21/2024
*/
static void start_sim(){
  // Initialize libcurl
  curl_global_init(CURL_GLOBAL_ALL);

  // Create a curl handle
  CURL* curl = curl_easy_init();
  if (curl) {

    //Reference fields by name
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:5432/Simulation/start");  //Send Curl PUT request to /Simulation/start command
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); //tells what kind of command this is

    // Perform the PUT request
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) { //Test for error. It shouldn't return an error since its all local host.
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
    }

    // Cleanup the curl handle
    curl_easy_cleanup(curl);
    } //Note: calls both curl_easy_cleanup and curl_global_cleanup to "clean" it from memory so that we can immediately use it again.
    curl_global_cleanup();
}

/* Function name: stop_sim() (/Simulation/stop PUT request to stop the simulation)
   Purpose: A function that sends the /Simulation/stopcommand to API
   Return type: Void
   Input Params: nothing 
   Works?: Confirmed it works 4/21/2024
*/
static void stop_sim(){
  // Initialize libcurl
  curl_global_init(CURL_GLOBAL_ALL);

  // Create a curl handle
  CURL* curl = curl_easy_init();
  if (curl) {
    
    //Reference fields by name
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:5432/Simulation/stop");  //Send Curl PUT request to /Simulation/start command
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); //tells what kind of command this is

    // Perform the PUT request
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) { //Test for error. It shouldn't return an error since its all local host.
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
    }

    // Cleanup the curl handle
    curl_easy_cleanup(curl);
    } //Note: calls both curl_easy_cleanup and curl_global_cleanup to "clean" it from memory so that we can immediately use it again.
    curl_global_cleanup();
}


/* Function name: nextPerson_get() (/Simulation GET request for the status)
   Purpose: A function that sends the /NextInput command to API to constantly get the next person in the string format
   Return type: string - returns the next person from the API in the format of a string
   Input Params: nothing 
   Works?: Confirmed it works 4/21/2024
*/
static string nextPerson_get() {  

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_ALL);

    // Create a new curl handle for the GET request
    // Create a curl handle
    CURL* curl = curl_easy_init();
    if (curl) {
        // Set the URL for the GET request
        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:5432/NextInput");
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback); //Overrides the writefuntion to use the writecallback function above

        // Create a string buffer to hold the response data
        std::string buffer;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

        // Perform the GET request
        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }

        // Cleanup the curl handle
        curl_easy_cleanup(curl);

        // Print the response body
        //std::cout << "\nResponse body: " << buffer << std::endl; //"buffer" is the data that was "get"

        //added returns the person in buffer
        return buffer;

    }
    // Cleanup libcurl
    curl_global_cleanup();
}


/* Function name: sim_get()
   Purpose: A function that performs a curl GET request to /Simulation API to check the status on the simulation (if its still alive or not)
   Return type: string - returns the status of the simulation from the API in a string format
   Input Params: nothing 
   Works?: Confirmed it works 4/21/2024
*/

static string sim_get(){
  // Initialize libcurl
  curl_global_init(CURL_GLOBAL_ALL);

  // Create a new curl handle for the GET request
  // Create a curl handle
  CURL* curl = curl_easy_init();
  if (curl) {
    
    // Set the URL for the GET request
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:5432/Simulation/check"); //Changed the command to "check" for simulation status
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback); //Overrides the writefuntion to use the writecallback function above

      // Create a string buffer to hold the response data
      std::string buffer;
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

      // Perform the GET request
      CURLcode res = curl_easy_perform(curl);

      //error message if curl doesn't get a reponse
      if (res != CURLE_OK) {
          std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
      }

      // Cleanup the curl handle
      curl_easy_cleanup(curl);

      // Print the response body
      //std::cout << "\nResponse body: " << buffer << std::endl; //"buffer" is the data that was "get"

      //this time, the buffer holds the string status of the simulation
      return buffer;

  }
  // Cleanup libcurl
  curl_global_cleanup();
}


/* Function name: Input_Communication_Thread()
   Purpose: Used to communicate with the /NextInput API. He recommends to insert a small wait after every attempt
   or evertime we get a NONE response from the API to prevent too many communication attempts to the API.
   Return type: Void
   Input Params: nothing 
   Works?: 
*/
static void Input_Communication_Thread() {
  /*2. Keep pinging the /NextInput API request. No data needs to be sent, it's a GET request so it constantly returns data
  The API will always return either:
      - The next person to be placed on a elevator
      - or the string "NONE" to signify that there isn't anyone to send yet. If it gets NONE, the code sleeps for half a second.
  */
  while(true){
    string sim_status = sim_get(); //Gets the status of the simulation

    if(sim_status == "Simulation is complete."){ //If the simulation is complete, then the entire program terminates
      stop_sim(); //Sends the PUT /Simulation/stop request to force stop the simulation
      exit(0); //Terminate entire program
    }
      
    //int line_num; // Represents every individual line in a file that has an int
    string next_person = nextPerson_get(); //Calls API to get the next person in a string format
      
    unique_lock<mutex> lock(mtx1); // gets mutex lock

    if(next_person == "NONE"){ //If the next person from the API returns "NONE" then tell the thread to wait before making another request (about half a second)
      this_thread::sleep_for(chrono::milliseconds(500));
      continue; //Continues the loop until it receives a person string
    }

    //Queue is full check
    while (personQ.size() == personQ_size) { // if the buffer queue is full, then tell this thread to wait with condition variable
      condV_Input.wait(lock); // automatically calls unlock when waiting for other threads, and locks when it "wakes up"
    }
      
      //integerQ.push(line_num); // Produces the data from the file (int) and pushes it into our buffer

    //Confirmed to work 4/22/2024, the delimited parsing method works in simulation debugging
    //Preprocess the string separated by "|":
    string delimiter = "|";
    size_t pos = 0;
    string segment; 
    vector<string> segmentlist; //Temp list to hold the split string
    while((pos = next_person.find(delimiter)) != string::npos){
      segment = next_person.substr(0,pos); //Get the segment from the string
      segmentlist.push_back(segment); //Add the split string to segmentlist
      next_person.erase(0, pos+delimiter.length()); //Erase the segment to get next segment
    }
    segmentlist.push_back(next_person); //Add the last element from the string to the list

    PersonObj temp_person; //Create a new person object
    temp_person.pName = segmentlist[0]; //First element will always be name
    //note to self: Use stoi to convert the strings into integers
    temp_person.start_floor = stoi(segmentlist[1]); //Starting floor position will be the second element
    temp_person.end_floor = stoi(segmentlist[2]); //Requested floor position will be the last element

    personQ.push(temp_person); //Add the person object into our queue
      
    
    lock.unlock(); // unlocks the mutex lock to share the buffer to the consumer
                 // when it wakes up

    condV_Scheduler.notify_one(); // wakes up the decision_maker thread to start "consuming"
                     // data and making decisions after producing


  }
}


/* Function name: Scheduler_Computation_Thread()
   Purpose: This is the decision maker thread that uses a scheduler algorithm to assign people to elevators whenever with the
   /NextInput API call. This thread should be pulling data from the input communication thread, then makes decisions,
   then pushes the data to the Output Communication Thread.
   note: I am going to try to implement the FIFO scheduling algorithm
   Return type: Void
   Input Params: nothing 
   Works?: 
*/
static void Scheduler_Computation_Thread() {
  
  while (true) { // while loop to consume data forever until it has to wait(consumes all the data in buffer)
    unique_lock<mutex> lock(mtx1); // gets mutex lock
    
    while (personQ.empty()) {    // if the personQ (buffer) is empty, wait for reader to read more data and fill buffer
      condV_Scheduler.wait(lock);
    }
    
    //Previous logic operations go after this line
    while(!personQ.empty()){
      unique_lock<mutex> lock2(mtx2); // gets mutex lock

      while (outputQ.size() == personQ_size) { // if the buffer queue is full, then tell this thread to wait with condition variable
        condV_Scheduler.wait(lock); // automatically calls unlock when waiting for other threads, and locks when it "wakes up"
      }

      PersonObj current_person = personQ.front(); //gets the first person object from the queue
      
      personQ.pop(); //removes the first element we just parsed, "consuming" it
      
      //First come first serve. Assign the first person to the front elevator of the queue IF the front elevator in the queue goes to the floor that they need, then move the elevator to the back
      //Of the queue?? Add logic to make sure that the person is getting on an elevator that has their floor
      ComboObj temp_combo;
      temp_combo.personID = current_person.pName; //Assign the current person's name (PersonID) to the temp_combo object.

      //while loop used to find an elevator for the current person thats within their range of travel
      while(true){
        ElevatorObj temp_elevator = eleDQ.front(); //gets front elevator in temporary variable
        eleDQ.pop_front(); //Remove the front elevator
        //If the person's start and end floor are within the range of the current elevator at the front of the queue, then assign that person to that elevator.
        if(current_person.start_floor <= temp_elevator.hFloor && current_person.start_floor >= temp_elevator.lFloor && current_person.end_floor <= temp_elevator.hFloor && current_person.end_floor >= temp_elevator.lFloor){
          temp_combo.elevatorID = temp_elevator.eName; //Assign this current person to current_elevator (elevatorID) if within range
          eleDQ.push_back(temp_elevator); //Move to the back of the queue once a person is assigned to it to mimic FIFO
          break; //Get out of the loop
        }
        else{ //If the front elevator is not in range with the person, then put this elevator to the back of the queue
          eleDQ.push_back(temp_elevator);
        }
      }

      //Add the temp_combo object to the outputQ
      outputQ.push(temp_combo); 

      //Unlock lock2
      lock2.unlock();
      
      condV_Output.notify_one(); //Wake up the output thread notifying that there is data in the outputQ

    }

    
    //Mutex unlocking 
    lock.unlock(); // Unlock the mutex whenever this consumes data so that we
                   // can wake up producer to produce more

    

    condV_Input.notify_one(); // Wake up reader to read more data if there is
                               // space available in integerQ queue
  }

}

/* Function name: Output_Communication_Thread()
   Purpose: This thread is used to communicate with the /AddPersonToElevator or /AddPersonToElevator_A3 API's. Its mainly used
   for requesting to the API to put the current person on that specific elevator after the Scheduler thread makes a decision.
   Return type: Void
   Input Params: nothing 
   Works?: 
*/
static void Output_Communication_Thread(){
  
  while(true){
    unique_lock<mutex> lock2(mtx2); // gets mutex lock
    
    while (outputQ.empty()) {    // if the outPersonQ (buffer) is empty, wait for reader to read more data and fill buffer
      condV_Output.wait(lock2);
    }
    
    ComboObj current_combo = outputQ.front(); //gets the front combo from the outputQ
    
    outputQ.pop(); //Consumes the first combo from the outputQ
  
    //Send the request for the person assigned to the elevator in FIFO to the API:
    add_person_to_ele(current_combo.personID, current_combo.elevatorID);
      
    lock2.unlock(); //Unlock lock2 

    condV_Scheduler.notify_one(); //Notify scheduler to keep producing data
  }
    
}




/*main function*/
int main(int argc, char *argv[]) {
  // Required that 3 threads need to run concurrently, no linear. Use .join() to
  // make sure that the code waits for all the threads to finish before killing
  // the program.


  // error Check for max number of arguments with error message and termination of program
  if (argc > max_args || argc == 1) {
      cout << "ERROR: Not enough arguments or too many arguments found. "
              "Terminating program..."
           << endl;
      return 0;
  }
  
  //Pull file name from argv (position 0 is the name of the file itself)
  char *fileN = argv[1];

  //Create fstream to read file argument and open it
  ifstream inputfile_arg;
  inputfile_arg.open(fileN);
  //inputfile_arg.open("highrise.bldg");

  // Check to see if file is open or not/if it exists
  if (inputfile_arg.is_open()) {
    //Temporary variables for the bldg files
    string elevatorName;
    int lowestFloor;
    int highestFloor;
    int currentFloor;
    int totalCapacity;

    //While loop reads the bldg file and separates the variables based on the spaces
    while(inputfile_arg >> elevatorName >> lowestFloor >> highestFloor >> currentFloor >> totalCapacity){
      //Create new elevator object
      ElevatorObj temp_elevator;

      //Set the elevator(processes) attributes to the inputfile
      temp_elevator.eName = elevatorName;
      temp_elevator.lFloor = lowestFloor;
      temp_elevator.hFloor = highestFloor;
      temp_elevator.cFloor = currentFloor;
      temp_elevator.t_capacity = totalCapacity;

      //Push elevator object into the elevator object deque list
      eleDQ.push_back(temp_elevator);
    }
  }
  else{ //error message if the bldg file is not found
    cout << "ERROR: File not found or invalid argument/filename. Terminating..." << endl;
    exit(0); // Exits program
  }

  //Test print to terminal for the list of elevators from the .bldg file
  //printlist(eleDQ);

  //1. Call the /Simulation/start to the API to start the elevator simulation:
  start_sim(); //Runs the curl code to start the simulation through the API command (PUT)
  
  // Spawn 3 threads to run concurrently:
  thread Input_thread(Input_Communication_Thread);
  thread Output_thread(Output_Communication_Thread);
  thread Scheduler_thread(Scheduler_Computation_Thread);
  

  // Make the program wait for the threads to finish before killing itself:
  //call 3 threads to run at the same time for the elevator OS: 
  Input_thread.join();
  Output_thread.join();
  Scheduler_thread.join();

  return 0;
}