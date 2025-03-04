# BitTorrent-Protocol
Harea Teodor-Adrian
333CA

APD - Tema 2
Protocolul BitTorrent

  In cadrul acestei teme am urmarit implementarea protocolului BitTorrent asa cum este
descris in enunt. Am urmat pasii indeaproape pentru a realiza functionarea corecta a 
programului. In urma implementarii, testele rulate de ./checker.sh au oferit rezultate
maxime.
  De asemenea, a fost realizata si eficienta transferului de fisiere. Acest lucru a fost
realizat prin faptul ca: la fiecare 10 segmente, pentru primele 5 segmente se va alege un
peer in mod aleator. Daca nu se reuseste descarcarea segmentului de la acel peer, se va
alege un seeder. Pentru urmatoarele 5 segmente se va alege un seeder in mod aleator pentru
descarcare.

  Trimitere date prin MPI: Fiecare thread (peer -> downloader/uploader sau tracker)
comunica cu celelalte procese prin MPI. Astfel, fiecare peer trimite si primeste date
intr-un mod prestabilit. Fiecare tip de thread are o structura specifica a bufferului
in care primeste informatii.

    Structura buffer pentru tracker:
        -> 1 int: tipul de mesaj
        -> 1 int: rank-ul procesului de la care a primit mesajul
        -> 1 int: numarul de fisiere pe care le are un uploader la inceput
        -> MAX_FILES * MAX_FILENAME: numele fisierelor detinute de uploader
        -> MAX_FILES * sizeof(int): numarul de chunk-uri pentru fiecare fisier detinut
        -> MAX_FILES * MAX_CHUNKS * HASH_SIZE: hash-urile pentru fiecare chunk
        -> MAX_FILENAME: numele fisierului pentru care un downloader cere informatii
                         sau numele fisierului pe care un downloader l-a descarcat complet

    Structura buffer pentru downloader: 
        -> 1 int: tipul de mesaj
        -> 1 int: rank-ul procesului de la care a primit mesajul
        -> 1 int: ack primit de la uploader; prin acest ack se simuleaza trimiterea unui chunk
                  de la uploader la downloader. Daca ack-ul este 1, downloader-ul a primit chunk-ul.
                  Daca ack-ul este 0, downloader-ul nu a primit chunk-ul deoarece uploaderul de la
                  care l-a cerut nu are chunk-ul respectiv.
        -> 1 int: numarul de peeri primiti de la tracker in urma cererii informatiilor despre un fisier
        -> MAX_PEERS * sizeof(int): rank-urile peerilor primiti de la tracker in urma cererii
                                    informatiilor despre un fisier
        -> 1 int: numarul de seederi primiti de la tracker in urma cererii informatiilor despre un fisier
        -> MAX_PEERS * sizeof(int): rank-urile seederilor primiti de la tracker in urma cererii
                                    informatiilor despre un fisier
        -> 1 int: numarul de chunk-uri necesare pentru fisierul pe care downloader-ul il descarca.
                  acest numar este primit de la tracker in urma cererii informatiilor despre un fisier
        -> MAX_CHUNKS * HASH_SIZE: hash-urile pentru chunk-urile necesare pentru fisierul pe care
                                   downloader-ul il descarca

    Structura buffer pentru uploader:
        -> 1 int: tipul de mesaj
        -> 1 int: rank-ul procesului de la care a primit mesajul
        -> MAX_FILENAME: numele fisierului pe care un downloader vrea sa il descarce
        -> HASH_SIZE: hash-ul pentru chunk-ul pe care un downloader il cere de la uploader
    
    Structuri folosite:
        -> struct peer_data: in urma crearii threadurilor de download si de upload pentru un peer,
                             cu ajutorul acestei structuri putem trimite date la ambele threaduri,
                             astfel incat acestea sa aiba informatii despre ce fisiere detine si
                             ce fisiere doreste sa descarce. Aceasta structura contine rank-ul
                            peer-ului, un vector de "file_t" cu informatii despre fisierele detinute
                            si un vector de string-uri cu numele fisierelor pe care doreste sa le
                            descarce.
        -> struct file_t: contine numele fisierului si hash-urile pentru fiecare chunk al fisierului
        -> struct swarm: aici se retin informatiile despre swarm-ul unui fisier. Aceasta structura
                         este folosita de tracker pentru a retine informatii despre fiecare fisier.
                         Structura aceasta contine numele fisierului, un vector de int-uri "clients"
                         cu rank-urile peerilor si seederilor care detin fisierul, un vector de int-uri
                         "seeders" cu rank-urile seederilor care detin fisierul, un vector de int-uri
                         "peers" cu rank-urile peerilor care detin fisierul si un vector de string-uri
                         "chunks" cu hash-urile pentru fiecare chunk al fisierului.

    Flow thread tracker:   Acesta gestioneaza informatiile despre swarm-uri. El primeste mesaje de la
                 threadurile de upload despre fisierele pe care le detine acel seeder si stocheaza
                 aceste informatii intr-un vector de swarm-uri, pentru fiecare fisier. Pentru acest
                 lucru se apeleaza functia "add_file_to_swarm". Dupa acest lucru, tracker-ul
                 verifica daca a primit informatii de la toate threadurile de upload. Daca da,
                 trimite mesaje catre threadurile de download pentru a le anunta ca pot incepe
                 descarcarea fisierelor.
                    De asemenea, tracker-ul primeste mesaje de la threadurile de download despre
                 fisierele pe care acestea vor sa le descarce. Tracker-ul trimite informatiile de
                 care are nevoie downloaderul, pe care le ia din vectorul de swarm-uri, in care
                 are detaliile despre fisierele detinute de seeders si peeri, precum si hash-urile
                 pentru fiecare chunk necesar descarcarii fisierului. Pentru acest lucru se apeleaza
                 functia "send_info_to_downloader", in care se si adauga rank-ul downloaderului in
                 lista de peeri care detin fisierul, dar si in lista "clients", unde se afla toti
                 seederii si peerii care detin fisierul.
                    O alta functionalitate a trackerului este de a primi mesaje de la downloaderi
                 in momentul in care ei au terminat de descarcat un anumit fisier. Atunci, trackerul
                 scoate rank-ul downloaderului din vectorul de peeri care detin fisierul si il adauga
                 in vectorul de seederi. Rank-ul downloaderului ramane in vectorul de clients, pentru
                 ca acest vector contine si seederii si peerii care detin fisierul.
                    In momentul in care un thread downloader al unui peer a terminat de descarcat 
                 toate fisierele pe care le dorea, trackerul primeste de la acesta un mesaj specific.
                 Trackerul tine cont de numarul de procese si in momentul in care toate threadurile
                 de download au terminat executia, trimite mesaje threadurilor de upload pentru ca
                 acestea sa se opreasca(functia "signal_uploader_threads_to_finish").  Dupa acest
                 lucru, se opreste si trackerul din executie.

    Flow thread downloader:  Acesta asteapta initial semnalul de la tracker ca poate incepe descarcarea
                 fiserelelor(functia "wait_for_ack").
                    Dupa ce primeste acest semnal, downloader-ul ia rand pe rand fisierele pe
                 care vrea sa le descarce si apeleaza functia "download_file" care se ocupa cu
                 descarcarea unui fisier.
                    In functia "download_file" se trimite o cerere de informatii despre fisier la tracker
                 initial sau o data la 10 chunk-uri descarcate(se foloseste variabila "counter").
                    Se realizeaza actualizarea vectorilor de peeri si seederi care detin fisierul cu ajutorul
                 functiilor "actualize_peers_with_file" si "actualize_seeders_with_file". Prima data cand
                 downloaderul primeste informatii despre fisier, actualizeaza vectorul de chunks_needed cu
                 hash-urile primite de la tracker (functia "actualize_chunks_needed").
                    Odata ce a primit informatiile necesare, functia "download_file" verifica daca vectorul
                 cu chunks_needed(completat initial din informatiile primite de la tracker) este gol.
                 Daca este gol inseamna ca a terminat de descarcat fisierul si scrie cu ajutorul functiei
                 "write_in_file" hash-urile chunk-urilor primite pentru fisierul respectiv in fisierul de
                 iesire.
                    Daca vectorul cu chunks_needed nu este gol, functia "download_file" va apela functia
                 "download_chunk" pentru a descarca un chunk de la un uploader. In aceasta functie se
                 alege un uploader in felul urmator: la fiecare 10 chunk-uri descarcate, pentru primele
                 5 chunk-uri se incearca descarcarea acestora de la peeri, nu de la seederi. Daca se primeste
                 ack egal cu 0 (nu am primit chunk-ul), se va face cerere de descarcare catre un seeder.
                 Pentru urmatoarele 5 chunk-uri, se vor face cereri direct la seederi. Alegerea rank-ului
                 seederilor sau a peerilor , in funtie de cazul in care ne aflam, se va face in mod aleator,
                 dar se va tine mereu cont de acest algoritm pentru a nu supraincarca uploaderii. Ideea acestui
                 mod de alegere a uploaderilor a fost luata de pe forumul temei 2. Dupa alegerea unui seed/peer,
                 se trimite un mesaj de cerere a chunk-ului scos cu "back" din vectorul "chunks_needed". Dupa 
                 ce se primeste un raspuns de la uploader, se verifica daca a fost primit chunk-ul cerut cu 
                 ajutorul ack-ului. Daca s-a primit chunk-ul, se adauga in vectorul "chunks_downloaded" si in
                 data->files_owned[fisier_curent].chunks. Functia "download_chunk" returneaza 1 daca
                 downloader-ul a primit chunk-ul si 0 daca nu a primit chunk-ul. De asemenea, daca chunk-ul
                 a fost descarcat cu succes, este scos din vectorul "chunks_needed".
                    Daca se primeste 1 ca si return al functiei "download_chunk", se incrementeaza
                 variabila "counter", care tine cont de cate chunk-uri au fost descarcate. Daca counter-ul
                 ajunge la 10, se trimite din nou o cerere de informatii despre fisierul curent catre tracker,
                 iar counter-ul devine 0.

    Flow thread uploader:   Se trimite initial mesaj catre tracker cu toate fisierele si chunk-urile detinute
                 de catre uploader. Intr-un while se asteapta mesaje de la downloaderi sau de la tracker.
                 Daca se primeste mesaj cu "type_of_message" 0, inseamna ca am primite mesaj de inchidere
                 de la tracker si se iese din while. Daca se primeste mesaj cu "type_of_message" 1, inseamna
                 ca am primit cerere de chunk de la un downloader. Se apeleaza functia "send_chunk_to_downloader",
                 care verifica daca uploader-ul detine chunk-ul cerut. Daca ne aflam in cazul in care detinem
                 chunk-ul cerut, se simuleaza trimiterea chunk-ului catre downloader cu ajutorul unui ack cu
                 valoarea 1. Daca nu detinem chunk-ul cerut, se trimite un ack cu valoarea 0.
