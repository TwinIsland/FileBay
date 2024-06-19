#define HASHMAP_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include "hashmap.h"
#include "mongoose.h"

#define CONFIG_FILE "CONFIG"
#define CONFIG_NUM_EXPECT 6

#define ASCII_LOGO_PATH "assets/ascii_logo"

#define SERIALIZE_VER 3 // version parameter, use to check serialzation version conflict

#define FILENODEPERMALLOC 5

#define HASHMAP_SIZE 256

#ifdef DEBUG
#define debug(msg, ...)                             \
    do                                              \
    {                                               \
        printf("(DEBUG) " msg "\n", ##__VA_ARGS__); \
    } while (0)
#else
#define debug(msg, ...)
#endif

#define ROUTER(router_name, ...) void router_##router_name(struct mg_connection *c, int ev, void *ev_data, \
                                                           struct mg_http_message *hm, ##__VA_ARGS__)

#define USE_ROUTER(router_name, ...) router_##router_name(c, ev, ev_data, hm, ##__VA_ARGS__)

static int service_should_stop = 0;
static pthread_t tid;
static struct MHD_Daemon *d;
static int nr_of_uploading_clients = 0;

static struct mg_mgr mgr;

// config parameter
static int file_max_byte, file_expire, worker_period_minute, file_max_count;
static char storage_dir[32], dump_dist[128];

static unsigned char serialization_ver = SERIALIZE_VER;

// web config
static const char *cached_exts[] = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".svg", ".js", ".css", ".ttf", NULL};

typedef struct FileNode
{
    int id;
    int is_del;
    char *file_name;
    size_t file_size;
    time_t expire_time;
    unsigned int pwd;
} FileNode;

static struct
{
    int sid;
    FileNode file_node;
} sid_buf = {
    .sid = -1,
};

static FileNode *FileNodeList;
static int FileNode_off = 0;
static int FileNode_max = 0;
static int FileNode_num = 0; // it may not equal to FileNode_off due to the dirty bit (is_del) design
static Hashmap *FileNode_hashmap;

static Hashmap *ws_timer_hashmap;

void print_logo()
{
    FILE *file = fopen(ASCII_LOGO_PATH, "r");
    if (!file)
        goto print_welcome;

    int ch;
    while ((ch = fgetc(file)) != EOF)
        putchar(ch);

    fclose(file);

print_welcome:
    printf("\n\nWelcome to use FileBay!\n\n");
}

int check_file_with_exts(const char *path, const char **exts)
{
    const char **ext;

    const char *dot = strrchr(path, '.');
    if (!dot)
    {
        return 0;
    }

    for (ext = exts; *ext; ext++)
    {
        if (strcmp(dot, *ext) == 0)
        {
            return 1;
        }
    }

    return 0;
}

/*
 * helper functions for managing file node list
 *
 */
unsigned int generate_rand_6digit()
{
    // Initialize random number generator the first time the function is called
    static int initialized = 0;
    if (!initialized)
    {
        srand(time(NULL));
        initialized = 1;
    }

    return 100000 + rand() % 900000;
}

FileNode create_FileNode(const char *file_name, size_t file_size)
{
    time_t cur_time;
    time(&cur_time);

    FileNode node = {
        .file_name = strdup(file_name),
        .file_size = file_size,
        .id = FileNode_off,
        .is_del = 0,
        .expire_time = cur_time + file_expire * 60,
        .pwd = generate_rand_6digit(),
    };

    return node;
}

FileNode create_standby_FileNode()
{
    time_t cur_time;
    time(&cur_time);

    FileNode node = {
        .id = FileNode_off,
        .is_del = 0,
        .expire_time = cur_time + file_expire * 60,
        .pwd = generate_rand_6digit(),
    };

    return node;
}

int add_FileNode(FileNode cur)
{
    // Allocate memory if not already done
    if (!FileNodeList)
    {
        FileNodeList = malloc(sizeof(FileNode) * FILENODEPERMALLOC);
        if (!FileNodeList)
        {
            perror("Failed to allocate memory for FileNodeList");
            return 1;
        }
        FileNode_off = 0;
        FileNode_max = FILENODEPERMALLOC;
    }

    // Check if there is space for a new node
    if (FileNode_off >= FileNode_max)
    {
        FileNode *temp = realloc(FileNodeList, sizeof(FileNode) * (FileNode_max + FILENODEPERMALLOC));
        if (!temp)
        {
            perror("Failed to reallocate memory for FileNodeList");
            return 1;
        }
        FileNodeList = temp;
        FileNode_max += FILENODEPERMALLOC;
    }

    // Copy the new node to the heap
    memcpy(FileNodeList + FileNode_off, &cur, sizeof(FileNode));
    hashmap_insert(FileNode_hashmap, cur.pwd, (void *)(FileNodeList + FileNode_off));
    debug("insert key: %d value: %p", cur.pwd, FileNodeList + FileNode_off);
    FileNode_num++;
    FileNode_off++;
    return 0;
}

FileNode *get_FileNode(unsigned int pwd)
{
    // attempt to get from hashmap, use brute force if collide
    FileNode *exp;

    exp = (FileNode *)hashmap_search(FileNode_hashmap, pwd);
    if (exp && exp->pwd == pwd)
    {
        debug("hit the hash map!");
        return exp;
    }
    else
    {
        return NULL;
    }

    if (FileNodeList)
    {
        for (int i = 0; i < FileNode_off; ++i)
        {
            if (FileNodeList[i].pwd == pwd)
            {
                if (FileNodeList[i].is_del)
                    return NULL;

                return (FileNode *)&FileNodeList[i];
            }
        }
    }
    return NULL;
}

void freeFileNodeList()
{
    if (FileNodeList)
    {
        for (int i = 0; i < FileNode_off; ++i)
        {
            free(FileNodeList[i].file_name);
        }

        free(FileNodeList);

        FileNodeList = NULL;
        FileNode_num = 0;
        FileNode_off = 0;
    }
}

int serialize_FileNodeList()
{
    FILE *file = fopen(dump_dist, "wb");
    if (!file)
    {
        perror("Failed to open file for writing");
        return -1;
    }

    // dump the file node list version
    fwrite(&serialization_ver, sizeof(unsigned char), 1, file);

    for (int i = 0; i < FileNode_off; i++)
    {
        if (FileNodeList[i].is_del)
        {
            // ignore if the FileNode marked as delete
            continue;
        }
        // Serialize id, file_size, expire_time as before
        fwrite(&FileNodeList[i].id, sizeof(int), 1, file);
        fwrite(&FileNodeList[i].is_del, sizeof(int), 1, file);
        fwrite(&FileNodeList[i].file_size, sizeof(size_t), 1, file);
        fwrite(&FileNodeList[i].expire_time, sizeof(time_t), 1, file);
        fwrite(&FileNodeList[i].pwd, sizeof(unsigned int), 1, file);

        // Serialize file_name
        size_t name_length = strlen(FileNodeList[i].file_name) + 1; // +1 for null terminator
        fwrite(&name_length, sizeof(size_t), 1, file);
        fwrite(FileNodeList[i].file_name, sizeof(char), name_length, file);
    }

    fclose(file);
    printf("file node list serialized to: %s\n", dump_dist);
    return 0; // Success
}

/*
 * deserialize the local dump, and initialize the FileNodeList
 *
 */
int deserialize_FileNodeList()
{
    FILE *file = fopen(dump_dist, "rb");
    if (!file)
        return 0;

    FileNode node;
    size_t name_length;
    int ret;

    // check for version mismatched
    unsigned char dist_serialization_ver;
    ret = fread(&dist_serialization_ver, sizeof(unsigned char), 1, file);
    if (ret && dist_serialization_ver != serialization_ver)
    {
        fprintf(stderr, "dist dump version %d mismatched with %d\n", dist_serialization_ver, serialization_ver);
        fclose(file);
        return 1;
    }

    while (fread(&node.id, sizeof(int), 1, file) == 1 &&
           fread(&node.is_del, sizeof(int), 1, file) == 1 &&
           fread(&node.file_size, sizeof(size_t), 1, file) == 1 &&
           fread(&node.expire_time, sizeof(time_t), 1, file) == 1 &&
           fread(&node.pwd, sizeof(unsigned int), 1, file) == 1)
    {
        fread(&name_length, sizeof(size_t), 1, file);
        node.file_name = malloc(name_length * sizeof(char));
        fread(node.file_name, sizeof(char), name_length, file);

        add_FileNode(node);
    }

    fclose(file);
    printf("file node list deserialized from: %s with size %d\n", dump_dist, FileNode_off);
    return 0; // Success
}

/*
 * Initialize config parameter
 * Returns: 1 if something goes wrong
 *
 */
int config_initialize()
{
    FILE *file = fopen(CONFIG_FILE, "r");
    if (file == NULL)
    {
        perror("Error opening config file");
        return 1;
    }

    char line[128];

    size_t config_count = 0;

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(line, "file_max_byte:%d", &file_max_byte) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "file_max_count:%d", &file_max_count) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "file_expire:%d", &file_expire) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "worker_period:%d", &worker_period_minute) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "storage_dir:%s", storage_dir) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "dump_dist:%s", dump_dist) == 1)
        {
            config_count++;
        }
        else
        {
            fprintf(stderr, "WARNING: invalid config line read: %s\n", line);
        }
    }
    fclose(file);

    if (config_count != CONFIG_NUM_EXPECT)
    {
        fprintf(stderr, "config load error, expect '%d' but have '%zu'\n", CONFIG_NUM_EXPECT, config_count);
        return 1;
    }

    return 0;
}

/*
 * Worker to clean the expired or unknown files
 * no heap use, kill it as you wish
 *
 */

void *cleaner_worker()
{
    while (!service_should_stop)
    {
        debug("cleaner worker wake up");
        if (FileNodeList)
        {
            time_t current_time;
            time(&current_time);
            char filepath[160];

            for (int i = 0; i < FileNode_off; ++i)
            {
                debug("file_id %d file_name %s expire %ld current %ld is_del %d",
                      FileNodeList[i].id, FileNodeList[i].file_name, FileNodeList[i].expire_time, current_time, FileNodeList[i].is_del);

                if (FileNodeList[i].is_del == 0 && FileNodeList[i].expire_time <= current_time)
                {
                    snprintf(filepath, sizeof(filepath), "%s/%d", storage_dir, FileNodeList[i].id);

                    if (unlink(filepath) != 0)
                    {
                        fprintf(stderr, "(Worker) Error deleting file %s: %s\n", FileNodeList[i].file_name, strerror(errno));
                    }
                    else
                    {
                        printf("(Worker) removing expired (%ld) file: %s\n", current_time - FileNodeList[i].expire_time, FileNodeList[i].file_name);
                    }
                    FileNode_num--;
                    FileNodeList[i].is_del = 1;

                    // ensure the file to delete match and ensure we can assert item not exist
                    // if cannot find its key in hashmap.
                    FileNode *target = hashmap_search(FileNode_hashmap, FileNodeList[i].pwd);
                    if (target && target->pwd == FileNodeList[i].pwd)
                        hashmap_delete(FileNode_hashmap, FileNodeList[i].pwd);
                }
            }
        }

        debug("cleaner worker sleep");

        // sleep untile another period
        sleep(worker_period_minute * 60);
    }

    return NULL;
}

/*
 * terminate handler for interupt signal and final cleanup
 *
 */
void terminate_handler()
{
    service_should_stop = 1;
    printf("cleaner thread end\n");
    mg_mgr_free(&mgr);
    printf("server stoped\n");
    serialize_FileNodeList();
    freeFileNodeList();
    freeHashmap(FileNode_hashmap);
    freeHashmap(ws_timer_hashmap);
    printf("bye\n");
    exit(0);
}

ROUTER(index_page)
{

    // Cache all image request
    char uri[hm->uri.len + 1];
    strncpy(uri, hm->uri.buf, hm->uri.len);
    uri[hm->uri.len] = '\0';

    debug("request: %s", uri);

    struct mg_http_serve_opts opts = {.root_dir = "assets", .page404 = "assets/index.html"};

#ifndef DEBUG
    if (check_file_with_exts(uri, cached_exts))
    {
        debug("cache file: %s", uri);
        opts.extra_headers = "Cache-Control: max-age=259200\n";
    }
#else
    (void)cached_exts; // disable warning
#endif

    mg_http_serve_dir(c, hm, &opts); // Serve static files
}

ROUTER(apply)
{
    if (sid_buf.sid == -1)
    {
        sid_buf.sid = generate_rand_6digit();
        sid_buf.file_node = create_standby_FileNode();
        mg_http_reply(c, 200, "", "{%m: %d, %m: %d}\n",
                      MG_ESC("status"), 1,
                      MG_ESC("code"), sid_buf.sid);
    }
    else
    {
        mg_http_reply(c, 501, "", "{%m: %d, %m: %m}\n",
                      MG_ESC("status"), 0,
                      MG_ESC("code"), MG_ESC("Service is busy"));
    }
}

ROUTER(upload)
{
    if (FileNode_num >= file_max_count)
    {
        mg_http_reply(c, 503, "", "{%m: %d, %m: %m}\n",
                      MG_ESC("status"), 0,
                      MG_ESC("code"), MG_ESC("Service is Unavailable"));
        return;
    }

    char buf[32];
    int ret = mg_http_get_var(&hm->query, "sid", buf, sizeof(buf));

    if (ret <= 0)
        mg_http_reply(c, 400, "", "Wrong Request");

    if (atoi(buf) == sid_buf.sid)
    {
        char filename[2] = {sid_buf.file_node.id + '0', '\0'};
        sid_buf.file_node.file_size = mg_http_upload(c, hm, filename, &mg_fs_posix, storage_dir, file_max_byte);

        if (sid_buf.file_node.file_size < 0)
        {
            unlink(filename);
            sid_buf.sid = -1;
            return;
        }
    }
    else
        mg_http_reply(c, 501, "", "Wrong SID");
}

ROUTER(download)
{
    char buf[32];
    int ret = mg_http_get_var(&hm->query, "pass", buf, sizeof(buf));
    struct mg_http_serve_opts opts = {};
    FileNode *filenode;

    if ((filenode = get_FileNode(atoi(buf))))
    {
        char *extra_header = malloc(128);
        printf("request download file: %s\n", filenode->file_name);
        sprintf(extra_header, "Content-Disposition: attachment; filename=%s\n", filenode->file_name);
        opts.extra_headers = extra_header;
        
        char local_path[64];
        sprintf(local_path, "%s/%d", storage_dir, filenode->id);

        mg_http_serve_file(c, hm, local_path, &opts);
        free(extra_header);
    }
    else
    {
        mg_http_reply(c, 404, "", "");
    }
}

ROUTER(finalizer)
{
    if (sid_buf.sid == -1)
    {
        mg_http_reply(c, 400, "", "");
        return;
    }

    char buf[64];
    mg_http_get_var(&hm->query, "sid", buf, sizeof(buf));

    if (atoi(buf) == sid_buf.sid)
    {
        mg_http_get_var(&hm->query, "file", buf, sizeof(buf));
        sid_buf.file_node.file_name = strdup(buf);
        add_FileNode(sid_buf.file_node);

        mg_http_reply(c, 200, "", "{%m: %d, %m: %d}\n",
                      MG_ESC("status"), 1,
                      MG_ESC("code"), sid_buf.file_node.pwd);

        sid_buf.sid = -1;
        printf("finalize upload file: %s\n", buf);
    }
    else
    {
        mg_http_reply(c, 501, "", "{%m: %d, %m: %m}\n",
                      MG_ESC("status"), 0,
                      MG_ESC("code"), MG_ESC("Wrong Sid"));
    }
}

ROUTER(config)
{
    mg_http_reply(c, 200, "", "{%m: %d, %m: %d}\n",
                  MG_ESC("file_max_byte"), file_max_byte,
                  MG_ESC("file_expire"), file_expire);
}

void ws_status_timer_fn(void *data)
{
    int is_busy = sid_buf.sid != -1 || FileNode_num >= file_max_count;
    char ret[2] = {is_busy + '0', '\0'};
    mg_ws_send((struct mg_connection *)data, &ret, 1, WEBSOCKET_OP_TEXT);
}

void server_fn(struct mg_connection *c, int ev, void *ev_data)
{
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    struct mg_str caps[3]; // router argument buffer

    if (ev == MG_EV_HTTP_MSG)
    {
        if (mg_match(hm->uri, mg_str("/api/config"), NULL))
            USE_ROUTER(config);

        else if (mg_match(hm->uri, mg_str("/api/apply"), NULL))
            USE_ROUTER(apply);

        else if (mg_match(hm->uri, mg_str("/api/upload"), NULL))
            USE_ROUTER(upload);

        else if (mg_match(hm->uri, mg_str("/api/finalizer#"), NULL))
            USE_ROUTER(finalizer);

        else if (mg_match(hm->uri, mg_str("/api/download"), NULL))
            USE_ROUTER(download);

        else if (mg_match(hm->uri, mg_str("/api/status"), NULL))
            mg_ws_upgrade(c, hm, NULL);

        else
            USE_ROUTER(index_page);
    }
    else if (ev == MG_EV_WS_OPEN)
    {
        struct mg_timer *t = mg_timer_add(&mgr, 1000, MG_TIMER_REPEAT, ws_status_timer_fn, (void *)c);
        if (hashmap_insert(ws_timer_hashmap, c->id, (void *)t))
        {
            // unlucky :(
            mg_timer_free(&mgr.timers, t);
        }
    }
    else if (ev == MG_EV_CLOSE && c->is_websocket)
    {
        struct mg_timer *t = hashmap_search(ws_timer_hashmap, c->id);
        mg_timer_free(&mgr.timers, t);
        hashmap_delete(ws_timer_hashmap, c->id);
    }
}

int main(int argc, char **argv)
{

    // signal handling
    signal(SIGINT, terminate_handler);
    signal(SIGTERM, terminate_handler);

    if (argc != 2)
    {
        fprintf(stderr, "%s PORT\n", argv[0]);
        return 1;
    }

    print_logo();

    // initialize global config parameter
    if (config_initialize() == 1)
    {
        return EXIT_FAILURE;
    }

    if (access(storage_dir, F_OK))
    {
        if (mkdir(storage_dir, 0700) == -1)
        { // 0700 sets read, write, and execute permissions for the owner
            perror("Error creating directory");
            return 1;
        }
        else
        {
            printf("Directory created: %s\n", storage_dir);
        }
    }

    // initialize the FileNode hashmap
    FileNode_hashmap = createHashmap(HASHMAP_SIZE);

    // initialize the ws_timer hashmap
    ws_timer_hashmap = createHashmap(HASHMAP_SIZE);
    // initialize old file node list
    deserialize_FileNodeList();

    // start server
    char server_addr[32];
    sprintf(server_addr, "http://127.0.0.1:%d", atoi(argv[1]));

    mg_mgr_init(&mgr);

    if (!mg_http_listen(&mgr, server_addr, (mg_event_handler_t)server_fn, NULL))
    {
        fprintf(stderr, "can't listen on port %d", atoi(argv[1]));
        return 1;
    }

    // Create the worker thread
    if (pthread_create(&tid, NULL, cleaner_worker, NULL) != 0)
    {
        perror("Error creating thread\n");
        return EXIT_FAILURE;
    }
    pthread_detach(tid);

    printf("Server start at %s\n", server_addr);

    while (!service_should_stop)
        mg_mgr_poll(&mgr, 1000); // Infinite event loop

    terminate_handler(-1);
    return 0;
}
