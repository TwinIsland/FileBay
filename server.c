#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>

#include <sys/stat.h>

// comment in out in production environment
#define debug 1
// #define test 1

#define CONFIG_FILE "CONFIG"
#define CONFIG_NUM_EXPECT 5

#define FileNodePerMalloc 5

static int thread_should_stop = 0;
static pthread_t tid;
static struct MHD_Daemon *d;

// config parameter
static long file_max_byte, file_expire, worker_period_minute;
static char storage_dir[128], dump_dist[128];

// version parameter, use to check serialzation version conflict
static int serialization_ver = '1';

struct ConnectionInfo
{
    FILE *fp;
    size_t total_size;
    struct MHD_PostProcessor *post_processor;
    int file_too_large_flag;
};

typedef struct FileNode
{
    int id;
    char *file_name;
    size_t file_size;
    time_t expire_time;
} FileNode;

static FileNode *FileNodeHead;
static int FileNode_off = 0;
static int FileNode_max = 0;

/*
 * helper functions for managing file node list
 *
 */
void add_FileNode(FileNode *cur)
{
    // Allocate memory if not already done
    if (!FileNodeHead)
    {
        FileNodeHead = malloc(sizeof(FileNode) * FileNodePerMalloc);
        if (!FileNodeHead)
        {
            perror("Failed to allocate memory for FileNodeHead");
            return;
        }
        FileNode_off = 0;
        FileNode_max = FileNodePerMalloc;
    }

    // Check if there is space for a new node
    if (FileNode_off >= FileNode_max)
    {
        FileNode *temp = realloc(FileNodeHead, sizeof(FileNode) * (FileNode_max + FileNodePerMalloc));
        if (!temp)
        {
            perror("Failed to reallocate memory for FileNodeHead");
            return;
        }
        FileNodeHead = temp;
        FileNode_max += FileNodePerMalloc;
    }

    // Copy the new node to the correct location
    memcpy(FileNodeHead + FileNode_off, cur, sizeof(FileNode));
    FileNode_off++;
}

void free_FileNode_list()
{
    if (FileNodeHead)
    {
        for (int i = 0; i < FileNode_off; ++i)
        {
            free(FileNodeHead[i].file_name);
        }

        free(FileNodeHead);

        FileNodeHead = NULL;
        FileNode_off = 0;
    }
}

int serializeFileNodeList()
{
    FILE *file = fopen(dump_dist, "wb");
    if (!file)
    {
        perror("Failed to open file for writing");
        return -1;
    }

    // dump the file node list version
    fwrite(&serialization_ver, sizeof(int), 1, file);

    for (int i = 0; i < FileNode_off; i++)
    {
        // Serialize id, file_size, expire_time as before
        fwrite(&FileNodeHead[i].id, sizeof(int), 1, file);
        fwrite(&FileNodeHead[i].file_size, sizeof(size_t), 1, file);
        fwrite(&FileNodeHead[i].expire_time, sizeof(time_t), 1, file);

        // Serialize file_name
        size_t name_length = strlen(FileNodeHead[i].file_name) + 1; // +1 for null terminator
        fwrite(&name_length, sizeof(size_t), 1, file);
        fwrite(FileNodeHead[i].file_name, sizeof(char), name_length, file);
    }

    fclose(file);
    printf("file node list serialized to: %s\n", dump_dist);
    return 0; // Success
}

int deserializeFileNodeList()
{
    FILE *file = fopen(dump_dist, "rb");
    if (!file)
        return 0;

    FileNode node;
    size_t name_length;
    int ret;

    // check for version mismatched
    int dist_serialization_ver;
    ret = fread(&dist_serialization_ver, sizeof(int), 1, file);
    if (dist_serialization_ver != serialization_ver)
    {
        fprintf(stderr, "dist dump version %d mismatched with %d\n", dist_serialization_ver, serialization_ver);
        fclose(file);
        return 1;
    }

    while (fread(&node.id, sizeof(int), 1, file) == 1 &&
           fread(&node.file_size, sizeof(size_t), 1, file) == 1 &&
           fread(&node.expire_time, sizeof(time_t), 1, file) == 1)
    {
        ret = fread(&name_length, sizeof(size_t), 1, file);
        node.file_name = malloc(name_length * sizeof(char));
        ret = fread(node.file_name, sizeof(char), name_length, file);
        (void)ret;
        add_FileNode(&node);
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
        if (sscanf(line, "file_max_byte:%ld", &file_max_byte) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "file_expire:%ld", &file_expire) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "worker_period:%ld", &worker_period_minute) == 1)
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
    }
    fclose(file);

    if (config_count != CONFIG_NUM_EXPECT)
    {
        perror("config load error");
        return 1;
    }

    return 0;
}

/*
 * Worker to clean the expired or unknown files
 * no heap use, kill it as you wish
 *
 */

void *cleaner_worker(void *arg)
{
    while (!thread_should_stop)
    {
#ifdef debug
        printf("(DEBUG) cleaner worker wake up\n");
#endif

        // create storage_dir if need
        struct stat st = {0};
        if (stat(storage_dir, &st) == -1)
        {
            // Directory does not exist, create it
            if (mkdir(storage_dir, 0700) == -1)
            {
                perror("Error creating directory");
                exit(EXIT_FAILURE);
            }
        }

        // open the directory
        DIR *dir = opendir(storage_dir);
        if (dir == NULL)
        {
            perror("Error opening directory");
            exit(EXIT_FAILURE);
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            // Skip "." and ".." entries
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            // Construct the full path of the file
            char file_path[128];
            snprintf(file_path, sizeof(file_path), "%s/%s", storage_dir, entry->d_name);

            // Delete the file
            if (remove(file_path) == 0)
            {
                printf("Worker: Deleted file: %s\n", file_path);
            }
            else
            {
                perror("Worker: Error deleting file");
            }
        }

        // Close the directory
        closedir(dir);

#ifdef debug
        printf("(DEBUG) cleaner worker sleep\n");
#endif

        // sleep untile another period
        sleep(worker_period_minute * 60);
    }

    return NULL;
}

/*
 * terminate handler for interupt signal and final cleanup
 *
 */
void terminate_handler(int signum)
{
    thread_should_stop = 1;
    pthread_cancel(tid);
    printf("cleaner thread end\n");
    MHD_stop_daemon(d);
    printf("server stoped\n");
    free_FileNode_list();
    serializeFileNodeList();
    printf("bye\n");
    exit(0);
}

/*
 * help function to check if the response body can be compressed or not
 * Returns: MHD_Result enum
 *
 */

static enum MHD_Result can_compress(struct MHD_Connection *con)
{
    const char *ae;
    const char *de;

    ae = MHD_lookup_connection_value(con,
                                     MHD_HEADER_KIND,
                                     MHD_HTTP_HEADER_ACCEPT_ENCODING);
    if (NULL == ae)
        return MHD_NO;
    if (0 == strcmp(ae,
                    "*"))
        return MHD_YES;
    de = strstr(ae,
                "deflate");
    if (NULL == de)
        return MHD_NO;
    if (((de == ae) ||
         (de[-1] == ',') ||
         (de[-1] == ' ')) &&
        ((de[strlen("deflate")] == '\0') ||
         (de[strlen("deflate")] == ',') ||
         (de[strlen("deflate")] == ';')))
        return MHD_YES;
    return MHD_NO;
}

/*
 * compress the response body
 * Returns: MHD_Result
 *
 */
static enum MHD_Result body_compress(void **buf,
                                     size_t *buf_size)
{
    Bytef *cbuf;
    uLongf cbuf_size;
    int ret;

    cbuf_size = compressBound(*buf_size);
    cbuf = malloc(cbuf_size);
    if (NULL == cbuf)
        return MHD_NO;
    ret = compress(cbuf,
                   &cbuf_size,
                   (const Bytef *)*buf,
                   *buf_size);
    if ((Z_OK != ret) ||
        (cbuf_size >= *buf_size))
    {
        /* compression failed */
        free(cbuf);
        return MHD_NO;
    }

    free(*buf);
    *buf = (void *)cbuf;
    *buf_size = (size_t)cbuf_size;
    return MHD_YES;
}

static enum MHD_Result response_file(struct MHD_Connection *connection, char *file_path)
{
    struct MHD_Response *response;
    enum MHD_Result ret;
    enum MHD_Result comp;

    FILE *file;
    long file_size;
    char *file_buf;

    printf("GET: %s\n", file_path);

    // load response content
    file = fopen(file_path, "rb");
    if (NULL == file)
    {
        printf("local file: %s cannot be find\n", file_path);
        return MHD_NO;
    }

    // get response size
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    rewind(file);

    // put response content on heap
    file_buf = malloc(file_size);
    if (NULL == file_buf)
    {
        fclose(file);
        return MHD_NO;
    }

    if (fread(file_buf, 1, file_size, file) != file_size)
    {
        fclose(file);
        free(file_buf);
        return MHD_NO;
    }

    fclose(file);

    /*
        try to compress the body,
        the following code comes from sample,
        do not modifying!
    */
    comp = MHD_NO;
    if (MHD_YES ==
        can_compress(connection))
        comp = body_compress((void **)&file_buf,
                             (size_t *)&file_size);

    response = MHD_create_response_from_buffer((size_t)file_size,
                                               file_buf,
                                               MHD_RESPMEM_MUST_FREE);
    if (NULL == response)
    {
        free(file_buf);
        return MHD_NO;
    }

    if (MHD_YES == comp)
    {
        if (MHD_NO ==
            MHD_add_response_header(response,
                                    MHD_HTTP_HEADER_CONTENT_ENCODING,
                                    "deflate"))
        {
            MHD_destroy_response(response);
            return MHD_NO;
        }
    }
    ret = MHD_queue_response(connection,
                             200,
                             response);
    MHD_destroy_response(response);
    return ret;
}

/*
 * Response with json, be aware that parameter 'content' should always be on stack
 * Returns: MHD_Result
 *
 */
static enum MHD_Result response_json(struct MHD_Connection *connection, char *content)
{
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(content), (void *)content, MHD_RESPMEM_MUST_COPY);
    if (response == NULL)
        return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "application/json");
    int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

static enum MHD_Result upload_req_callback(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
                                           const char *filename, const char *content_type,
                                           const char *transfer_encoding, const char *data,
                                           uint64_t off, size_t size)
{
    struct ConnectionInfo *con_info = coninfo_cls;

    // printf("file type: %s\n", key);
    // Only process file upload fields
    if (0 != strcmp(key, "file"))
    {
        return MHD_YES;
    }

    if (!con_info->fp)
    {
        char file_path[FILENAME_MAX];
        snprintf(file_path, sizeof(file_path), "%s/%s", storage_dir, filename);

        con_info->fp = fopen(file_path, "ab");
        if (!con_info->fp)
        {
            return MHD_NO; // Cannot open file
        }
    }

    if (size > 0)
    {
        if (con_info->total_size + size > file_max_byte)
        {
            return MHD_NO; // Exceeds file size limit
        }
        fwrite(data, 1, size, con_info->fp);
        con_info->total_size += size;
    }

    return MHD_YES;
}

static enum MHD_Result upload_req_handler(struct MHD_Connection *connection, const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    enum MHD_Result result;

    // first time post, create ConnectionInfo
    if (NULL == *con_cls)
    {
        struct ConnectionInfo *con_info = malloc(sizeof(struct ConnectionInfo));
        if (NULL == con_info)
            return MHD_NO;

        con_info->fp = NULL;
        con_info->total_size = 0;
        con_info->post_processor = MHD_create_post_processor(connection, 64 * 1024, &upload_req_callback, con_info);

        if (NULL == con_info->post_processor)
        {
            free(con_info);
            return MHD_NO;
        }

        *con_cls = (void *)con_info;
        return MHD_YES;
    }

    // upload file
    struct ConnectionInfo *con_info = *con_cls;
    if (*upload_data_size)
    {
        if (con_info->total_size + *upload_data_size > file_max_byte)
        {
            printf("file too large\n");
            con_info->file_too_large_flag = 1;
            // Drain the remaining data
            *upload_data_size = 0;
            return MHD_YES;
        }

        MHD_post_process(con_info->post_processor, upload_data, *upload_data_size);
        con_info->total_size += *upload_data_size;
        *upload_data_size = 0;
        return MHD_YES;
    }
    else // request complete, do some cleaning
    {
        result = con_info->file_too_large_flag ? MHD_HTTP_CONTENT_TOO_LARGE : MHD_YES;

        if (con_info != NULL)
        {
            // Clean up resources
            if (con_info->fp)
                fclose(con_info->fp);

            if (con_info->post_processor)
                MHD_destroy_post_processor(con_info->post_processor);

            free(con_info);
            *con_cls = NULL;
        }
        else
        {
            printf("but why\n");
            return MHD_NO;
        }

        return result;
    }
}

/*
 * Main entry for handling the web request
 * Returns: MHD_Result
 *
 */
static enum MHD_Result ahc_echo(void *cls,
                                struct MHD_Connection *connection,
                                const char *url,
                                const char *method,
                                const char *version,
                                const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    char file_path[64];
    char resp_str_buffer[128];

    (void)cls;     /* Unused. Silent compiler warning. */
    (void)version; /* Unused. Silent compiler warning. */

    // request handler
    if (strcmp(method, "GET") == 0)
    {
        /*
            Get request handler
            1. response root index.html
            2. make css, js, pages folder as static
            3. server all files in storage_dir
            4. /max_size: return the max file size limit
        */
        if (!*con_cls)
        {
            *con_cls = (void *)1;
            return MHD_YES;
        }
        *con_cls = NULL;

        if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0)
        {
            strcpy(file_path, "./index.html");
        }
        else if (strncmp(url, "/css", 4) == 0 || strncmp(url, "/js", 3) == 0 || strncmp(url, "/pages", 6) == 0)
        {
            snprintf(file_path, sizeof(file_path), ".%s", url);
        }
        else if (strcmp(url, "/config") == 0)
        {
            sprintf(resp_str_buffer, "{\"file_max_byte\": %ld, \"file_expire\": %ld}", file_max_byte, file_expire);
            return response_json(connection, resp_str_buffer);
        }
        else
        {
            // invalid GET, reponse index.html to handle such situation
            printf("GET(X): %s\n", url);
            strcpy(file_path, "./index.html");
        }

        return response_file(connection, file_path);
    }
    else if (strcmp(method, "POST") == 0)
    {
        /*
            Post request handler
            1. /upload: file uploading router
            2. /file: get file given file id
        */
        if (strcmp(url, "/") == 0)
        {
            return upload_req_handler(connection, upload_data, upload_data_size, con_cls);
        }
        if (strcmp(url, "/file") == 0)
        {
            // TODO: finish it
        }

        // invalid POST, reponse index.html to handle such situation
        printf("POST(X): %s\n", file_path);
        strcpy(file_path, "./index.html");
        return response_file(connection, "./index.html");
    }

    return MHD_NO; /* unexpected method */
}

/*
 * test case for file node list
 *
 */
void test_FileNodeOperations()
{
    // Adding 12 FileNodes
    for (int i = 0; i < 12; i++)
    {
        FileNode node;
        node.file_name = strdup("file");
        add_FileNode(&node);
    }

    // Serialize the list
    if (serializeFileNodeList() != 0)
    {
        fprintf(stderr, "Serialization failed\n");
    }

    // Free the allocated memory
    free_FileNode_list();

    // deserialization test
    deserializeFileNodeList();
    printf("file node off: %d\nmax: %d\nhead: %s\n", FileNode_off, FileNode_max, FileNodeHead->file_name);
    free_FileNode_list();
}

int main(int argc, char *const *argv)
{

#ifdef debug
    printf("!!! DEBUG MODE: to switch to the production mode, comment out the line '#define debug 1'\n\n");
#endif

    // signal handling
    signal(SIGINT, terminate_handler);
    signal(SIGTERM, terminate_handler);

    if (argc != 2)
    {
        fprintf(stderr, "%s PORT\n", argv[0]);
        return 1;
    }

    // initialize global config parameter
    if (config_initialize() == 1)
    {
        perror("config file load error");
        return 1;
    }

    // initialize old file node list
    deserializeFileNodeList();

#ifdef test
    printf("!!! TEST MODE: to switch to the production mode, comment out the line '#define test 1'\n\n");
    test_FileNodeOperations();
    return 0;
#endif

    // start server
    d = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
                         atoi(argv[1]), NULL, NULL,
                         &ahc_echo, NULL,
                         MHD_OPTION_END);

    if (NULL == d)
    {
        perror("server start failed\n");
        return 1;
    }

    printf("* Server started. Press enter to stop.\n\n");

    // Create the worker thread
    if (pthread_create(&tid, NULL, cleaner_worker, NULL) != 0)
    {
        perror("Error creating thread\n");
        return EXIT_FAILURE;
    }

    (void)getchar(); // Wait for user input to terminate the server

    terminate_handler(-1);
    return 0;
}
