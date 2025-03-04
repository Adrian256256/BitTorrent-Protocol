#include <mpi.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <pthread.h>
#include <map>
#include <string>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <list>
#include <vector>
#include <set>
#include <queue>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <pthread.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define MAX_PEERS 100

using namespace std;

//structura in care primeste date uploaderul
/*
int type_of_message; //0 semnal de inchidere de la tracker, 1 cerere de download
int rank;
string wanted_filename;
string wanted_chunk;
*/

#define UPLOADER_RECV_TO_TYPEOFMESSAGE 0
#define UPLOADER_RECV_TO_RANK 4
#define UPLOADER_RECV_TO_WANTEDFILENAME 8
#define UPLOADER_RECV_TO_WANTEDCHUNK 8 + MAX_FILENAME
#define TOTAL_UPLOADER_RECV_SIZE 8 + MAX_FILENAME + HASH_SIZE

//structura in care primeste date downloaderul
/*
int type_of_message; //0 raspuns de la tracker, 1 raspuns de la un peer
int rank;
int ack; //0 -> nu am fisierul, 1 -> am fisierul | raspuns de la peer
int number_of_peers;
//array de int cu peerii
int peers[MAX_PEERS];
int number_of_chunks;
//array de string cu chunk-urile
string chunks[MAX_CHUNKS];
*/
#define DOWNLOADER_RECV_TO_TYPEOFMESSAGE 0
#define DOWNLOADER_RECV_TO_RANK 4
#define DOWNLOADER_RECV_TO_ACK 8
#define DOWNLOADER_RECV_TO_NUMBEROFPEERS 12
#define DOWNLOADER_RECV_TO_PEERS 16
#define DOWNLOADER_RECV_TO_NUMBEROFSEEEDERS 16 + MAX_PEERS * 4
#define DOWNLOADER_RECV_TO_SEEDERS 16 + MAX_PEERS * 4 + 4
#define DOWNLOADER_RECV_TO_NUMBEROFCHUNKS 16 + MAX_PEERS * 4 + 4 + MAX_PEERS * 4
#define DOWNLOADER_RECV_TO_CHUNKS 16 + MAX_PEERS * 4 + 4 + MAX_PEERS * 4 + 4
#define TOTAL_DOWNLOADER_RECV_SIZE 16 + MAX_PEERS * 4 + 4 + MAX_PEERS * 4 + 4 + MAX_CHUNKS * HASH_SIZE

//structura in care primeste date trackerul
/*
int type_of_message;
int rank;

//type_of_message = 0 -> datele despre fisierele detinute de un peer
int num_files_owned;
//array de string cu fisierele detinute
string files_owned[MAX_FILES];
//array de string cu chunk-urile detinute pentru fiecare fisier
string chunks_owned[MAX_FILES][MAX_CHUNKS];

//type_of_message = 1 -> cerere de download
string wanted_filename;
*/

#define TRACKER_RECV_TO_TYPEOFMESSAGE 0
#define TRACKER_RECV_TO_RANK 4
#define TRACKER_RECV_TO_NUMFILESOWNED 8
#define TRACKER_RECV_TO_FILESOWNED 12
#define TRACKER_RECV_TO_NUMBEROFCHUNKS 12 + MAX_FILES * MAX_FILENAME
#define TRACKER_RECV_TO_CHUNKSOWNED 12 + MAX_FILES * MAX_FILENAME + MAX_FILES * sizeof(int)
#define TRACKER_RECV_TO_WANTEDFILENAME 12 + MAX_FILES * MAX_FILENAME + MAX_FILES * sizeof(int) + MAX_CHUNKS * MAX_FILES * HASH_SIZE
#define TOTAL_TRACKER_RECV_SIZE 12 + MAX_FILES * MAX_FILENAME + MAX_FILES * sizeof(int) + MAX_CHUNKS * MAX_FILES * HASH_SIZE + MAX_FILENAME

typedef struct {
    char filename[MAX_FILENAME];
    vector<string> chunks;
} file_t;

//strucutra data ca input unui peer
typedef struct {
    int rank;
    //vector de structuri file_t cu fisierele detinute
    vector<file_t> files_owned;
    //vector de stringuri cu numele fisierelor dorite
    vector<string> files_wanted;
} peer_data;

//structura pentru un swarm
typedef struct {
    char filename[MAX_FILENAME];
    //vector de int-uri cu rank-urile clientilor
    //acestia sunt peers sau seed-eri
    vector<int> clients;
    //vector de seederi
    vector<int> seeders;
    //vector de peers
    vector<int> peers;
    //fac array de stringuri cu chunk-urile
    vector<string> chunks;
} swarm;

//functie de trimitere de la download la tracker cu terminare descarcari
void send_download_finished(int rank) 
{
    char *buffer = (char*) malloc(TOTAL_TRACKER_RECV_SIZE);
    int type_of_message = 1;
    memcpy(buffer + TRACKER_RECV_TO_TYPEOFMESSAGE, &type_of_message, 4);
    memcpy(buffer + TRACKER_RECV_TO_RANK, &rank, 4);
    MPI_Send(buffer, TOTAL_TRACKER_RECV_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
    free(buffer);
}

//functie de trimitere de la download la tracker cerere de fisier
void ask_tracker_for_file(int rank, string wanted_filename)
{
    char *buffer = (char*) malloc(TOTAL_TRACKER_RECV_SIZE);
    int type_of_message = 2;
    memcpy(buffer + TRACKER_RECV_TO_TYPEOFMESSAGE, &type_of_message, 4);
    memcpy(buffer + TRACKER_RECV_TO_RANK, &rank, 4);
    memcpy(buffer + TRACKER_RECV_TO_WANTEDFILENAME, wanted_filename.c_str(), MAX_FILENAME);
    MPI_Send(buffer, TOTAL_TRACKER_RECV_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
    free(buffer);
}

//functie de scriere in fisier a chunk-urilor descarcate
void write_in_file(string wanted_filename, vector<string> chunks_downloaded, int rank)
{
    string filename = "client" + to_string(rank) + "_" + wanted_filename;
    FILE *file = fopen(filename.c_str(), "w");
    for (int i = chunks_downloaded.size() - 1; i >= 0; i--) {
        //daca e ultimul chunk, nu mai pun newline
        if (i == 0) {
            char *buffer = (char*) malloc(HASH_SIZE + 1);
            memcpy(buffer, chunks_downloaded[i].c_str(), HASH_SIZE);
            //pun \n la finalul buffer-ului
            buffer[HASH_SIZE] = '\0';
            fprintf(file, "%s", buffer);
            free(buffer);
        } else {
            char *buffer = (char*) malloc(HASH_SIZE + 1);
            memcpy(buffer, chunks_downloaded[i].c_str(), HASH_SIZE);
            //pun \n la finalul buffer-ului
            buffer[HASH_SIZE] = '\0';
            fprintf(file, "%s\n", buffer);
            free(buffer);
        }
    }
    fclose(file);
}

//funtie care asteapta mesajul de ack de la tracker
//dupa ce primeste mesajul, se poate incepe descarcarea fisierelor
void wait_for_ack()
{
    char *buffer = (char*) malloc(TOTAL_DOWNLOADER_RECV_SIZE);
    MPI_Recv(buffer, TOTAL_DOWNLOADER_RECV_SIZE, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    free(buffer);
}

//functie care anunta trackerul ca un downloader a terminat descarcarea unui fisier
void send_download_finished_for_file(int rank, string wanted_filename)
{
    char *buffer = (char*) malloc(TOTAL_TRACKER_RECV_SIZE);
    int type_of_message = 3;
    memcpy(buffer + TRACKER_RECV_TO_TYPEOFMESSAGE, &type_of_message, 4);
    memcpy(buffer + TRACKER_RECV_TO_RANK, &rank, 4);
    //pun numele fisierului
    memcpy(buffer + TRACKER_RECV_TO_WANTEDFILENAME, wanted_filename.c_str(), MAX_FILENAME);
    MPI_Send(buffer, TOTAL_TRACKER_RECV_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
    free(buffer);
}

//functie care descarca un chunk
int download_chunk(int rank, string wanted_filename, string wanted_chunk, vector<int> peers_with_file, vector<int> seeders_with_file, vector<string> &chunks_downloaded, peer_data *data, vector<string> &chunks_needed, bool send_to_seeder)
{
    int peer = -1;
    if (!send_to_seeder) {
        //verific daca existe vreun peer , si daca cumva sunt numai eu
        if (peers_with_file.size() == 1 && peers_with_file[0] == rank) {
            return 0;
        }
        if (peers_with_file.size() == 0) {
            return 0;
        }
        while(true) {
            int random_peer = rand() % peers_with_file.size();
            if (peers_with_file[random_peer] != rank) {
                peer = peers_with_file[random_peer];
                break;
            }
        }
    } else {
        while(true) {
            int random_seeder = rand() % seeders_with_file.size();
            if (seeders_with_file[random_seeder] != rank) {
                peer = seeders_with_file[random_seeder];
                break;
            }
        }
    }

    //trimit cerere de download
    char *buffer = (char*) malloc(TOTAL_UPLOADER_RECV_SIZE);
    int type_of_message = 1;
    memcpy(buffer + UPLOADER_RECV_TO_TYPEOFMESSAGE, &type_of_message, 4);
    memcpy(buffer + UPLOADER_RECV_TO_RANK, &rank, 4);
    memcpy(buffer + UPLOADER_RECV_TO_WANTEDFILENAME, wanted_filename.c_str(), MAX_FILENAME);
    memcpy(buffer + UPLOADER_RECV_TO_WANTEDCHUNK, wanted_chunk.c_str(), HASH_SIZE);
    MPI_Send(buffer, TOTAL_UPLOADER_RECV_SIZE, MPI_CHAR, peer, 2, MPI_COMM_WORLD);
    free(buffer);
    //astept raspuns
    buffer = (char*) malloc(TOTAL_DOWNLOADER_RECV_SIZE);
    MPI_Recv(buffer, TOTAL_DOWNLOADER_RECV_SIZE, MPI_CHAR, peer, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    //raspuns de la peer
    int ack = buffer[DOWNLOADER_RECV_TO_ACK];
    if (ack == 1) {
        //am primit chunk-ul
        chunks_downloaded.push_back(wanted_chunk);
        chunks_needed.pop_back();
        //pun chunk-ul in data
        bool found = false;
        char *wanted_filename_c = (char*) malloc(MAX_FILENAME);
        memcpy(wanted_filename_c, wanted_filename.c_str(), MAX_FILENAME);
        for (long unsigned int i = 0; i < data->files_owned.size(); i++) {
            if (strcmp(data->files_owned[i].filename, wanted_filename_c) == 0) {
                data->files_owned[i].chunks.push_back(wanted_chunk);
                found = true;
                break;
            }
        }
        if (!found) {
            file_t new_file;
            strcpy(new_file.filename, wanted_filename_c);
            new_file.chunks.push_back(wanted_chunk);
            data->files_owned.push_back(new_file);
        }
        free(wanted_filename_c);
        free(buffer);
        return 1;
    } else {
        //nu am primit chunk-ul
        free(buffer);
        return 0;
    }
}

//functie care actualizeaza vectorul de peeri cu noii peeri primiti de la tracker
void actualize_peers_with_file(vector<int> &peers_with_file, char *buffer)
{
    //golesc vectorul
    peers_with_file.clear();
    int number_of_peers = *((int*)(buffer + DOWNLOADER_RECV_TO_NUMBEROFPEERS));
    for (int i = 0; i < number_of_peers; i++) {
        int peer = *((int*)(buffer + DOWNLOADER_RECV_TO_PEERS + i * sizeof(int)));
        peers_with_file.push_back(peer);
    }
}

//functie care actualizeaza vectorul de seed-eri cu noii seed-eri primiti de la tracker
void actualize_seeders_with_file(vector<int> &seeders_with_file, char *buffer)
{
    //golesc vectorul
    seeders_with_file.clear();
    int number_of_seeders = *((int*)(buffer + DOWNLOADER_RECV_TO_NUMBEROFSEEEDERS));
    for (int i = 0; i < number_of_seeders; i++) {
        int seeder = *((int*)(buffer + DOWNLOADER_RECV_TO_SEEDERS + i * sizeof(int)));
        seeders_with_file.push_back(seeder);
    }
}

//functie care actualizeaza vectorul de chunk-uri necesare pentru un fisier
void actualize_chunks_needed(vector<string> &chunks_needed, char *buffer)
{
    int number_of_chunks = *((int*)(buffer + DOWNLOADER_RECV_TO_NUMBEROFCHUNKS));
    for (int i = 0; i < number_of_chunks; i++) {
        char *chunk = (char*) malloc(HASH_SIZE + 1);
        memcpy(chunk, buffer + DOWNLOADER_RECV_TO_CHUNKS + i * HASH_SIZE, HASH_SIZE);
        //pun \n la finalul chunk-ului
        chunk[HASH_SIZE] = '\0';
        chunks_needed.push_back(string(chunk));
        free(chunk);
    }
}

//functie care descarca un fisier
void download_file(int rank, string wanted_filename, peer_data *data)
{
    //incep descarcarea acestui fisier
    vector<string> chunks_needed;
    vector<int> peers_with_file;
    vector<int> seeders_with_file;
    vector<string> chunks_downloaded;
    //counter, la fiecare 10 segmente descarcate, trimit cerere de actualizare informatii fisier catre tracker
    int counter = 10;
    while(true) {
        //daca am descarcat 10 segmente, trimit cerere de actualizare informatii fisier catre tracker
        //de asemenea, daca tocmai am inceput descarcarea fisierului, trimit cerere de download catre tracker
        if (counter == 10) {
            counter = 0;
            ask_tracker_for_file(rank, wanted_filename);
            char *buffer = (char*) malloc(TOTAL_DOWNLOADER_RECV_SIZE);
            MPI_Recv(buffer, TOTAL_DOWNLOADER_RECV_SIZE, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            //actualizez peers_with_file si seeders_with_file
            actualize_peers_with_file(peers_with_file, buffer);
            actualize_seeders_with_file(seeders_with_file, buffer);

            //pun ce e in seeders_with_file in peers_with_file
            for (long unsigned int i = 0; i < peers_with_file.size(); i++) {
                seeders_with_file.push_back(peers_with_file[i]);
            }

            //doar prima data cand primesc informatii despre fiser voi actualiza chunks_needed
            //informatiile despre chunk-uri vor ramane aceleasi pe parcursul descarcarii fisierului
            if (chunks_needed.size() == 0 && chunks_downloaded.size() == 0) {
                actualize_chunks_needed(chunks_needed, buffer);
            }

            free(buffer);
        }

        //daca am descarcat toate chunk-urile acestui fisier
        if (chunks_needed.size() == 0) {
            //trimit mesaj de terminare descarcare fisier catre tracker
            send_download_finished_for_file(rank, wanted_filename);

            //scriu in fisier
            write_in_file(wanted_filename, chunks_downloaded, rank);
            break;
        }

        //descarc un chunk
        string wanted_chunk = chunks_needed.back();
        if (counter < 5) {
            if (download_chunk(rank, wanted_filename, wanted_chunk, peers_with_file, seeders_with_file, chunks_downloaded, data, chunks_needed, false) == 1) {
            //am descarcat chunk-ul
            counter++;
            } else {
                //nu am descarcat chunk-ul
                //incerc sa descarc de la un seeder
                if (download_chunk(rank, wanted_filename, wanted_chunk, peers_with_file, seeders_with_file, chunks_downloaded, data, chunks_needed, true) == 1) {
                    //am descarcat chunk-ul
                    counter++;
                }
            }
        } else {
            //trimit direct la seederi
            if (download_chunk(rank, wanted_filename, wanted_chunk, peers_with_file, seeders_with_file, chunks_downloaded, data, chunks_needed, true) == 1) {
                //am descarcat chunk-ul
                counter++;
            }
        }
    }
}

void *download_thread_func(void *arg)
{
    peer_data *data = (peer_data*) arg;
    int rank = data->rank;
    vector<string> files_wanted = data->files_wanted;
    //astept sa primesc ack de la tracker inainte sa incep descarcarea fisierelor
    wait_for_ack();
    while(true) {
        if (files_wanted.size() == 0) {
            send_download_finished(rank);
            break;
        }

        //scot un fisier din lista de fisiere dorite
        string wanted_filename = files_wanted.back();
        files_wanted.pop_back();

        download_file(rank, wanted_filename, data);
    }
    
    return NULL;
}

//functie care creeaza un buffer pentru a trimite datele despre fisierele detinute catre tracker
char *create_buffer_upload_to_tracker(peer_data *data)
{
    char *buffer = (char*) malloc(TOTAL_TRACKER_RECV_SIZE);
    //setez type_of_message
    buffer[TRACKER_RECV_TO_TYPEOFMESSAGE] = 0;
    //setez rank-ul
    memcpy(buffer + TRACKER_RECV_TO_RANK, &data->rank, 4);
    //setez numarul de fisiere detinute
    int num_files_owned = data->files_owned.size();
    memcpy(buffer + TRACKER_RECV_TO_NUMFILESOWNED, &num_files_owned, 4);
    //setez fisierele detinute
    for (int i = 0; i < num_files_owned; i++) {
        //setez numele fisierului
        memcpy(buffer + TRACKER_RECV_TO_FILESOWNED + i * MAX_FILENAME, data->files_owned[i].filename, MAX_FILENAME);
    }
    //setez numarul de chunk-uri detinute pentru fiecare fisier
    for (int i = 0; i < num_files_owned; i++) {
        int number_of_chunks = data->files_owned[i].chunks.size();
        memcpy(buffer + TRACKER_RECV_TO_NUMBEROFCHUNKS + i * sizeof(int), &number_of_chunks, 4);
    }
    //setez chunk-urile detinute
    for (int i = 0; i < num_files_owned; i++) {
        for (long unsigned int j = 0; j < data->files_owned[i].chunks.size(); j++) {
            char *chunk = (char*) malloc(HASH_SIZE);
            memcpy(chunk, data->files_owned[i].chunks[j].c_str(), HASH_SIZE);
            memcpy(buffer + TRACKER_RECV_TO_CHUNKSOWNED + i * MAX_CHUNKS * HASH_SIZE + j * HASH_SIZE, chunk, HASH_SIZE);
            
            free(chunk);
        }
    }
    
    return buffer;
}

//functie de trimitere a unui chunk de la uploader la downloader
void send_chunk_to_downloader(char *buffer, peer_data *data)
{
    int rank = data->rank;
    int recv_rank = *((int*)(buffer + UPLOADER_RECV_TO_RANK));
    char *wanted_filename_t = (char*) malloc(MAX_FILENAME + 1);
    char *wanted_chunk_t = (char*) malloc(HASH_SIZE + 1);
    memcpy(wanted_filename_t, buffer + UPLOADER_RECV_TO_WANTEDFILENAME, MAX_FILENAME);
    wanted_filename_t[MAX_FILENAME] = '\0';
    memcpy(wanted_chunk_t, buffer + UPLOADER_RECV_TO_WANTEDCHUNK, HASH_SIZE);
    wanted_chunk_t[HASH_SIZE] = '\0';

    string wanted_filename = string(wanted_filename_t);
    string wanted_chunk = string(wanted_chunk_t);
    //verific daca am chunk-ul
    bool have_chunk = false;
    for (long unsigned int i = 0; i < data->files_owned.size(); i++) {
        if (strcmp(data->files_owned[i].filename, wanted_filename.c_str()) == 0) {
            for (long unsigned int j = 0; j < data->files_owned[i].chunks.size(); j++) {
                if (data->files_owned[i].chunks[j] == wanted_chunk) {
                    have_chunk = true;
                    break;
                }
            }
        }
    }
    //trimit raspuns
    char *buffer_to_send = (char*) malloc(TOTAL_DOWNLOADER_RECV_SIZE);
    int type_of_message = 1;
    memcpy(buffer_to_send + DOWNLOADER_RECV_TO_TYPEOFMESSAGE, &type_of_message, 4);
    memcpy(buffer_to_send + DOWNLOADER_RECV_TO_RANK, &rank, 4);
    if (have_chunk) {
        buffer_to_send[DOWNLOADER_RECV_TO_ACK] = 1;
    } else {
        buffer_to_send[DOWNLOADER_RECV_TO_ACK] = 0;
    }
    MPI_Send(buffer_to_send, TOTAL_DOWNLOADER_RECV_SIZE, MPI_CHAR, recv_rank, 1, MPI_COMM_WORLD);
    free(buffer);
}

//functie de trimitere a datelor initiale catre tracker
void send_initial_data_to_tracker(peer_data *data)
{
    char *buffer = create_buffer_upload_to_tracker(data);
    MPI_Send(buffer, TOTAL_TRACKER_RECV_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
    free(buffer);
}

void *upload_thread_func(void *arg)
{
    peer_data *data = (peer_data*) arg;
    
    //trimit datele despre fisierele detinute catre tracker
    send_initial_data_to_tracker(data);

    while(true) {
        char *buffer = (char*) malloc(TOTAL_UPLOADER_RECV_SIZE);
        MPI_Recv(buffer, TOTAL_UPLOADER_RECV_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        int type_of_message = buffer[UPLOADER_RECV_TO_TYPEOFMESSAGE];
        if (type_of_message == 0) {
            //mesaj de terminare de la tracker, inchid thread-ul
            free(buffer);
            break;
        }
        if (type_of_message == 1) {
            //mesaj de cerere de download
            send_chunk_to_downloader(buffer, data);
        }
    }
    return NULL;
}

//functie de adaugare a fisierelor primite in tracker in swarm-uri
void add_files_to_swarms(vector<swarm> &swarms, char *buffer)
{
    int rank = *((int*)(buffer + TRACKER_RECV_TO_RANK));
    int num_files_owned = *((int*)(buffer + TRACKER_RECV_TO_NUMFILESOWNED));
    string files_owned[MAX_FILES];
    for (int i = 0; i < num_files_owned; i++) {
        string filename = string(buffer + TRACKER_RECV_TO_FILESOWNED + i * MAX_FILENAME);
        files_owned[i] = filename;
    }
    //scot nr de chunks 
    int number_of_chunks[MAX_FILES];
    for (int i = 0; i < num_files_owned; i++) {
        number_of_chunks[i] = *((int*)(buffer + TRACKER_RECV_TO_NUMBEROFCHUNKS + i * sizeof(int)));
    }
    
    //adaug fisierele in swarm-uri
    for (int i = 0; i < num_files_owned; i++) {
        bool found = false;
        for (long unsigned int j = 0; j < swarms.size(); j++) {
            if (strcmp(swarms[j].filename, files_owned[i].c_str()) == 0) {
                found = true;
                swarms[j].clients.push_back(rank);
                swarms[j].seeders.push_back(rank);
                break;
            }
        }
        if (!found) {
            swarm new_swarm;
            strcpy(new_swarm.filename, files_owned[i].c_str());
            new_swarm.clients.push_back(rank);
            new_swarm.seeders.push_back(rank);
            //adaug clientul in lista de seederi
            new_swarm.seeders.push_back(rank);
            for (int j = 0; j < number_of_chunks[i]; j++) {
                char *chunk = (char*) malloc(HASH_SIZE + 1);
                memcpy(chunk, buffer + TRACKER_RECV_TO_CHUNKSOWNED + i * MAX_CHUNKS * HASH_SIZE + j * HASH_SIZE, HASH_SIZE);
                //pun \n la finalul chunk-ului
                chunk[HASH_SIZE] = '\0';
                new_swarm.chunks.push_back(string(chunk));
                free(chunk);
            }
            swarms.push_back(new_swarm);
        }
    }
    free(buffer);
}

//functie de trimitere a datelor de download de la tracker la downloader
void send_info_to_downloader(char *buffer, vector<swarm> &swarms)
{
    int rank = *((int*)(buffer + TRACKER_RECV_TO_RANK));
    string wanted_filename = string(buffer + TRACKER_RECV_TO_WANTEDFILENAME);
    //adaug downloaderul in lista de peers a fisierului din swarnms, daca nu este deja
    char *wanted_filename_c = (char*) malloc(MAX_FILENAME);
    memcpy(wanted_filename_c, wanted_filename.c_str(), MAX_FILENAME);
    for (long unsigned int i = 0; i < swarms.size(); i++) {
        if (strcmp(swarms[i].filename, wanted_filename_c) == 0) {
            bool found = false;
            for (long unsigned int j = 0; j < swarms[i].clients.size(); j++) {
                int client = swarms[i].clients[j];
                int rank = *((int*)(buffer + TRACKER_RECV_TO_RANK));
                if (client == rank) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                swarms[i].clients.push_back(rank);
                swarms[i].peers.push_back(rank);
            }
            break;
        }
    }

    int number_of_peers = 0;
    int peers[MAX_PEERS];
    int number_of_seeders = 0;
    int seeders[MAX_PEERS];
    int number_of_chunks = 0;
    string chunks[MAX_CHUNKS];
    for (long unsigned int i = 0; i < swarms.size(); i++) {
        if (strcmp(swarms[i].filename, wanted_filename_c) == 0) {
            number_of_peers = swarms[i].peers.size();
            for (long unsigned int j = 0; j < swarms[i].peers.size(); j++) {
                peers[j] = swarms[i].peers[j];
            }
            number_of_seeders = swarms[i].seeders.size();
            for (long unsigned int j = 0; j < swarms[i].seeders.size(); j++) {
                seeders[j] = swarms[i].seeders[j];
            }
            number_of_chunks = swarms[i].chunks.size();
            for (long unsigned int j = 0; j < swarms[i].chunks.size(); j++) {
                chunks[j] = swarms[i].chunks[j];
            }
            break;
        }
    }
    char *buffer_to_send = (char*) malloc(TOTAL_DOWNLOADER_RECV_SIZE);
    int type_of_message = 0;
    memcpy(buffer_to_send + DOWNLOADER_RECV_TO_TYPEOFMESSAGE, &type_of_message, 4);
    memcpy(buffer_to_send + DOWNLOADER_RECV_TO_RANK, &rank, 4);
    memcpy(buffer_to_send + DOWNLOADER_RECV_TO_NUMBEROFPEERS, &number_of_peers, 4);
    for (int i = 0; i < number_of_peers; i++) {
        memcpy(buffer_to_send + DOWNLOADER_RECV_TO_PEERS + i * sizeof(int), &peers[i], 4);
    }
    memcpy(buffer_to_send + DOWNLOADER_RECV_TO_NUMBEROFSEEEDERS, &number_of_seeders, 4);
    for (int i = 0; i < number_of_seeders; i++) {
        memcpy(buffer_to_send + DOWNLOADER_RECV_TO_SEEDERS + i * sizeof(int), &seeders[i], 4);
    }
    memcpy(buffer_to_send + DOWNLOADER_RECV_TO_NUMBEROFCHUNKS, &number_of_chunks, 4);
    for (int i = 0; i < number_of_chunks; i++) {
        // memcpy(buffer_to_send + DOWNLOADER_RECV_TO_CHUNKS + i * HASH_SIZE, chunks[i].c_str(), HASH_SIZE);
        char *chunk = (char*) malloc(HASH_SIZE);
        memcpy(chunk, chunks[i].c_str(), HASH_SIZE);
        memcpy(buffer_to_send + DOWNLOADER_RECV_TO_CHUNKS + i * HASH_SIZE, chunk, HASH_SIZE);
        free(chunk);
    }
    MPI_Send(buffer_to_send, TOTAL_DOWNLOADER_RECV_SIZE, MPI_CHAR, rank, 1, MPI_COMM_WORLD);
    free(buffer_to_send);
    free(buffer);
    free(wanted_filename_c);
}

//functie de anuntare inchidere pentru thread-urile de upload
void signal_uploader_threads_to_finish(int numtasks)
{
    for (int i = 1; i < numtasks; i++) {
        char *buffer = (char*) malloc(TOTAL_UPLOADER_RECV_SIZE);
        int type_of_message = 0;
        memcpy(buffer + UPLOADER_RECV_TO_TYPEOFMESSAGE, &type_of_message, 4);
        MPI_Send(buffer, TOTAL_UPLOADER_RECV_SIZE, MPI_CHAR, i, 2, MPI_COMM_WORLD);
        free(buffer);
    }
}

void tracker(int numtasks, int rank) {
    vector<swarm> swarms;
    int num_threads_finished = 0;
    int count = 0;
    while(true) {
        //primesc date de la un peer
        char *buffer = (char*) malloc(TOTAL_TRACKER_RECV_SIZE);
        MPI_Recv(buffer, TOTAL_TRACKER_RECV_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        int type_of_message = buffer[TRACKER_RECV_TO_TYPEOFMESSAGE];
        if (type_of_message == 0) {
            //mesaj cu fisierele detinute de un peer
            count++;
            add_files_to_swarms(swarms, buffer);
            //daca count == numtasks - 1, atunci am primit datele de la toti seed-erii
            if (count == numtasks - 1) {
                //trimit mesaj de incepere download
                for (int i = 1; i < numtasks; i++) {
                    char *buffer_2 = (char*) malloc(TOTAL_DOWNLOADER_RECV_SIZE);
                    buffer_2[DOWNLOADER_RECV_TO_TYPEOFMESSAGE] = 5;
                    MPI_Send(buffer_2, TOTAL_DOWNLOADER_RECV_SIZE, MPI_CHAR, i, 1, MPI_COMM_WORLD);
                    free(buffer_2);
                }
            }
            continue;
        }
        if (type_of_message == 2) {
            //mesaj de cerere de download
            send_info_to_downloader(buffer, swarms);
            continue;
        }
        if (type_of_message == 1) {
            //mesaj de terminare download
            num_threads_finished++;
            if (num_threads_finished == numtasks - 1) {
                free(buffer);
                signal_uploader_threads_to_finish(numtasks);
                break;
            }
            free(buffer);
        }
        if (type_of_message == 3) {
            //mesaj ca un downloader a terminat descarcarea unui fisier
            //il scot din peers si il bag la seeds la acel fisier
            int rank = *((int*)(buffer + TRACKER_RECV_TO_RANK));
            string wanted_filename = string(buffer + TRACKER_RECV_TO_WANTEDFILENAME);
            for (long unsigned int i = 0; i < swarms.size(); i++) {
                if (strcmp(swarms[i].filename, wanted_filename.c_str()) == 0) {
                    //scot din lista de peers
                    for (long unsigned int j = 0; j < swarms[i].peers.size(); j++) {
                        if (swarms[i].peers[j] == rank) {
                            swarms[i].peers.erase(swarms[i].peers.begin() + j);
                            break;
                        }
                    }
                    //adaug in lista de seeds
                    swarms[i].seeders.push_back(rank);
                    break;
                }
            }
            free(buffer);
        }
    }
    
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    //deschidere fisier
    char filename[35];
    // sprintf(filename, "../checker/tests/test1/in%d.txt", rank);
    sprintf(filename, "in%d.txt", rank);
    FILE *file = fopen(filename, "r");
    //fisierul se afla la ../checker/tests/test1/in0.txt

    if (file == NULL) {
        printf("Eroare la deschiderea fisierului\n");
        exit(-1);
    }
    //citesc fisierele
    int num_files_owned;
    fscanf(file, "%d", &num_files_owned);
    //citesc numele fisierelor si le pun intr-un vector de structuri file_t
    vector<file_t> files;
    for (int i = 0; i < num_files_owned; i++) {
        file_t file_now;
        fscanf(file, "%s", file_now.filename);
        int num_chunks;
        fscanf(file, "%d", &num_chunks);
        for (int j = 0; j < num_chunks; j++) {
            char *chunk = (char*) malloc(HASH_SIZE + 1);
            //citesc maxim HASH_SIZE caractere
            fscanf(file, "%s", chunk);
            file_now.chunks.push_back(string(chunk));
            free(chunk);
        }
        files.push_back(file_now);
    }
    //citesc fisierele dorite
    int num_files_wanted;
    fscanf(file, "%d", &num_files_wanted);
    //citesc numele fisierelor dorite
    vector<string> files_wanted;
    for (int i = 0; i < num_files_wanted; i++) {
        char filename[MAX_FILENAME];
        fscanf(file, "%s", filename);
        files_wanted.push_back(filename);
    }

    //inchidere fisier
    fclose(file);
    //structura cu datele unui peer
    peer_data data;
    data.rank = rank;
    data.files_owned = files;
    data.files_wanted = files_wanted;

    r = pthread_create(&download_thread, NULL, download_thread_func, (void*) &data);
    if (r) {
        printf("Eroare la crearea thread-ului download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void*) &data);
    if (r) {
        printf("Eroare la crearea thread-ului upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului upload\n");
        exit(-1);
    }

}

int main(int argc, char *argv[]) {
    int numtasks, rank;
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
    return 0;
}