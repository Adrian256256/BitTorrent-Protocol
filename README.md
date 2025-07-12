# BitTorrent-Protocol

**Author:** Harea Teodor-Adrian  
**Title:** BitTorrent Protocol

In this project, I implemented the BitTorrent protocol as described in the assignment specification. I closely followed the outlined steps to ensure correct functionality. After implementation, the test suite run by `./checker.sh` yielded maximum scores.

Additionally, the efficiency of file transfers was considered. This was achieved by dividing download requests into intervals of 10 segments:
- For the first 5 segments: a random peer is selected. If the segment cannot be downloaded from this peer, a seeder is used.
- For the next 5 segments: a random seeder is chosen directly for downloading.

---

## MPI Communication

Each thread (peer → downloader/uploader or tracker) communicates with other processes via **MPI**. Each peer sends and receives data in a predefined format, with each thread type using a specific buffer structure.

### Tracker Buffer Structure:
- `1 int`: message type
- `1 int`: rank of the sender process
- `1 int`: number of files an uploader owns initially
- `MAX_FILES * MAX_FILENAME`: filenames of owned files
- `MAX_FILES * sizeof(int)`: number of chunks for each owned file
- `MAX_FILES * MAX_CHUNKS * HASH_SIZE`: hash of each chunk
- `MAX_FILENAME`: filename being requested or the one fully downloaded

### Downloader Buffer Structure:
- `1 int`: message type
- `1 int`: rank of the sender
- `1 int`: ack received from uploader (1 = success, 0 = chunk not owned)
- `1 int`: number of peers received from the tracker
- `MAX_PEERS * sizeof(int)`: ranks of peers with the file
- `1 int`: number of seeders received from the tracker
- `MAX_PEERS * sizeof(int)`: ranks of seeders
- `1 int`: number of required chunks for the file
- `MAX_CHUNKS * HASH_SIZE`: hash for each required chunk

### Uploader Buffer Structure:
- `1 int`: message type
- `1 int`: sender's rank
- `MAX_FILENAME`: requested filename
- `HASH_SIZE`: hash of the requested chunk

---

## Data Structures Used

- `struct peer_data`: used to pass data between upload and download threads of a peer. Contains peer rank, vector of `file_t` (owned files), and vector of strings (wanted files).
- `struct file_t`: contains the filename and the chunk hashes.
- `struct swarm`: used by the tracker to store file info. Contains:
  - filename
  - vector of `clients` (all peers/seeders owning the file)
  - vector of `seeders`
  - vector of `peers`
  - vector of chunk hashes

---

## Tracker Thread Flow

- Manages all swarm information.
- Receives data from uploader threads with file and chunk info, using `add_file_to_swarm`.
- After all uploader info is received, notifies downloader threads to start.
- On downloader requests, uses `send_info_to_downloader` to send peer/seeder ranks and chunk hashes.
- Updates swarm by adding downloader to the list of clients/peers.
- When a downloader finishes a file, moves it from `peers` to `seeders` in the swarm.
- When all downloaders complete all downloads, calls `signal_uploader_threads_to_finish` to notify uploaders to exit, then exits itself.

---

## Downloader Thread Flow

- Waits for a start signal from the tracker using `wait_for_ack`.
- Downloads files sequentially using `download_file`.
- Every 10 chunks downloaded, it requests updated swarm info from the tracker.
- Updates peer/seeder vectors using `actualize_peers_with_file` and `actualize_seeders_with_file`.
- Fills `chunks_needed` with hashes using `actualize_chunks_needed`.
- If `chunks_needed` is empty, writes downloaded hashes to output using `write_in_file`.
- Otherwise, downloads chunks using `download_chunk`:
  - Every 10 chunks: first 5 from peers, fallback to seeders; next 5 directly from seeders.
  - Downloader sends request to uploader, checks `ack`.
  - If successful (`ack == 1`), updates `chunks_downloaded` and file ownership.
- After 10 chunks, resets counter and re-requests tracker info.

---

## Uploader Thread Flow

- Initially sends all file/chunk info to the tracker.
- Waits in a loop for messages:
  - If message type = 0 → shutdown signal from tracker → exit.
  - If message type = 1 → chunk request from downloader → process using `send_chunk_to_downloader`.
    - If chunk is owned → send ack = 1.
    - Otherwise → ack = 0.
