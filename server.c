#define _POSIX_C_SOURCE 200809L
#define HASHMAP_IMPLEMENTATION

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
#include <fcntl.h>
#include <errno.h>

#include "hashmap.h"

#define CONFIG_FILE "CONFIG"
#define CONFIG_NUM_EXPECT 7

#define FILENODEPERMALLOC 5
#define POSTBUFFERSIZE 512

#define DIR_BUFFER_SIZE 128
#define STD_MSG_BUFFER_SIZE 128

#ifdef DEBUG
#define debug(msg, ...)                             \
    do                                              \
    {                                               \
        printf("(DEBUG) " msg "\n", ##__VA_ARGS__); \
    } while (0)
#else
#define debug(msg, ...)
#endif

static int thread_should_stop = 0;
static pthread_t tid;
static struct MHD_Daemon *d;
static int nr_of_uploading_clients = 0;

// config parameter
static int file_max_byte, file_expire, worker_period_minute, max_file_count, max_client;
static char storage_dir[DIR_BUFFER_SIZE], dump_dist[DIR_BUFFER_SIZE];

// version parameter, use to check serialzation version conflict
static unsigned char serialization_ver = 2;

typedef struct FileNode
{
    int id;
    int is_del;
    char *file_name;
    size_t file_size;
    time_t expire_time;
    unsigned int pwd;
} FileNode;

typedef struct ConnectionInfo
{
    FILE *fp;
    char file_dir[DIR_BUFFER_SIZE];
    struct MHD_PostProcessor *post_processor;
    FileNode node;
    size_t upload_byte;
    int is_err;
    char err_msg[STD_MSG_BUFFER_SIZE];
} ConnectionInfo;

static FileNode *FileNodeList;
static int FileNode_off = 0;
static int FileNode_max = 0;
static int FileNode_num = 0; // it may not equal to FileNode_off due to the dirty bit (is_del) design
static Hashmap *FileNode_hashmap;

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
    } else {
        return NULL;
    }

    debug("not hit, use brute force");

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
        ret = fread(&name_length, sizeof(size_t), 1, file);
        node.file_name = malloc(name_length * sizeof(char));
        ret = fread(node.file_name, sizeof(char), name_length, file);
        (void)ret;
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

    char line[STD_MSG_BUFFER_SIZE];

    size_t config_count = 0;

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(line, "file_max_byte:%d", &file_max_byte) == 1)
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
        else if (sscanf(line, "max_file_count:%d", &max_file_count) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "max_client:%d", &max_client) == 1)
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
        fprintf(stderr, "config load error\n");
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
    while (!thread_should_stop)
    {
        debug("cleaner worker wake up");
        if (FileNodeList)
        {
            time_t current_time;
            time(&current_time);
            char filepath[DIR_BUFFER_SIZE];

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
    thread_should_stop = 1;
    printf("cleaner thread end\n");
    MHD_stop_daemon(d);
    printf("server stoped\n");
    serialize_FileNodeList();
    freeFileNodeList();
    freeHashmap(FileNode_hashmap);
    printf("bye\n");
    exit(0);
}

/*
 * help function to check if the response body can be compressed or not
 * Returns: MHD_Result enum
 *
 */

static enum MHD_Result
can_compress(struct MHD_Connection *con)
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
static enum MHD_Result
body_compress(void **buf,
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

static enum MHD_Result
response_err(struct MHD_Connection *connection, char *invalidRequestMessage, int err_code)
{
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(invalidRequestMessage),
                                                                    (void *)invalidRequestMessage,
                                                                    MHD_RESPMEM_PERSISTENT);
    int ret = MHD_queue_response(connection, err_code, response);
    MHD_destroy_response(response);
    return ret;
}

static enum MHD_Result
response_resource(struct MHD_Connection *connection, const char *file_path)
{
    struct MHD_Response *response;
    enum MHD_Result ret;
    enum MHD_Result comp;

    FILE *file;
    size_t file_size;
    char *file_buf;

    debug("GET: %s", file_path);

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

static enum MHD_Result
response_file(struct MHD_Connection *connection, unsigned int pwd)
{
    int fd;
    struct stat sbuf;
    FileNode *target;
    char file_path[DIR_BUFFER_SIZE];
    char content_disposition[DIR_BUFFER_SIZE];

    if (pwd < 100000 || pwd > 999999)
    {
        debug("GET(X): /file/%d\n", pwd);
        return response_err(connection, "Invalid Request", MHD_HTTP_BAD_REQUEST);
    }

    target = get_FileNode(pwd);

    if (target)
    {
        sprintf(file_path, "%s/%d", storage_dir, target->id);
    }
    else
    {
        // request file no exist
        return response_err(connection, "File not found", MHD_HTTP_NOT_FOUND);
    }

    fd = open(file_path, O_RDONLY);
    if (fd < 0)
        return MHD_NO; // Failed to open file

    if (fstat(fd, &sbuf) < 0)
    {
        close(fd);
        return MHD_NO; // Failed to stat file
    }

    struct MHD_Response *response;
    response = MHD_create_response_from_fd_at_offset64(sbuf.st_size, fd, 0);
    if (response == NULL)
    {
        close(fd);
        return MHD_NO; // Failed to create response
    }

    snprintf(content_disposition, sizeof(content_disposition),
             "attachment; filename=\"%s\"", target->file_name);

    MHD_add_response_header(response, "Content-Disposition", content_disposition);

    int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

/*
 * Response with json, be aware that parameter 'content' should always be on stack
 * Returns: MHD_Result
 *
 */
static enum MHD_Result
response_json(struct MHD_Connection *connection, char *content, int status_code)
{
    struct MHD_Response *response = MHD_create_response_from_buffer(strlen(content), (void *)content, MHD_RESPMEM_MUST_COPY);
    if (response == NULL)
        return MHD_NO;

    MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, "application/json");
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

/*
 * Method to handle file uploading
 * https://android.googlesource.com/platform/external/libmicrohttpd/+/master/doc/examples/largepost.c
 *
 */
static enum MHD_Result
upload_req_callback(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
                    const char *filename, const char *content_type,
                    const char *transfer_encoding, const char *data,
                    uint64_t off, size_t size)
{
    ConnectionInfo *con_info = coninfo_cls;
    FILE *fp;
    char local_file_dir[DIR_BUFFER_SIZE];
    FileNode new_file_node;

    (void)kind;
    (void)content_type;
    (void)transfer_encoding;
    (void)off;

    if (0 != strcmp(key, "file"))
        return MHD_NO;

    if (!con_info->fp)
    {
        // check if limitation satisfied
        if (nr_of_uploading_clients >= max_client)
        {
            con_info->is_err = 1;
            sprintf(con_info->err_msg, "Server is busy");
            return MHD_NO;
        }

        if (max_file_count < FileNode_num + 1)
        {
            con_info->is_err = 1;
            sprintf(con_info->err_msg, "Currently Unavailable");
            return MHD_NO;
        }

        // initialize new file node
        new_file_node = create_FileNode(filename, 0);
        con_info->node = new_file_node;

        sprintf(local_file_dir, "%s/%d", storage_dir, new_file_node.id);

        strcpy(con_info->file_dir, local_file_dir);

        if (NULL != (fp = fopen(local_file_dir, "rb")))
        {
            fclose(fp);
            return MHD_NO;
        }
        con_info->fp = fopen(local_file_dir, "ab");
        if (!con_info->fp)
            return MHD_NO;
    }
    if (size > 0)
    {
        con_info->upload_byte += size;

        // debug("upload: %ld", con_info->upload_byte);

        if (con_info->is_err || con_info->upload_byte > (size_t)file_max_byte)
        {
            con_info->is_err = 1;
            sprintf(con_info->err_msg, "File too large");
            return MHD_NO;
        }

        if (!fwrite(data, size, sizeof(char), con_info->fp))
            return MHD_NO;
    }
    return MHD_YES;
}

static void
request_completed(void *cls, struct MHD_Connection *connection,
                  void **con_cls, enum MHD_RequestTerminationCode toe)
{
    ConnectionInfo *con_info = *con_cls;

    (void)cls;
    (void)connection;
    (void)toe;

    if (NULL == con_info)
        return;

    if (NULL != con_info->post_processor)
    {
        MHD_destroy_post_processor(con_info->post_processor);
        nr_of_uploading_clients--;
    }
    if (con_info->fp)
    {
        fclose(con_info->fp);
    }

    free(con_info);
    *con_cls = NULL;
}

static enum MHD_Result
upload_req_handler(struct MHD_Connection *connection, const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    char ret_buffer[STD_MSG_BUFFER_SIZE];
    ConnectionInfo *con_info;

    if (NULL == *con_cls)
    {
        con_info = malloc(sizeof(ConnectionInfo));

        if (NULL == con_info)
            return MHD_NO;

        con_info->fp = NULL;
        con_info->is_err = 0;
        con_info->upload_byte = 0;
        con_info->post_processor =
            MHD_create_post_processor(connection, POSTBUFFERSIZE,
                                      upload_req_callback, (void *)con_info);

        if (NULL == con_info->post_processor)
        {
            free(con_info);
            return MHD_NO;
        }
        nr_of_uploading_clients++;

        *con_cls = (void *)con_info;
        return MHD_YES;
    }

    con_info = *con_cls;
    if (0 != *upload_data_size)
    {
        MHD_post_process(con_info->post_processor, upload_data,
                         *upload_data_size);
        *upload_data_size = 0;
        return MHD_YES;
    }
    else
    {
        if (NULL != con_info->fp)
        {
            fclose(con_info->fp);
            con_info->fp = NULL;
        }

        /* Now it is safe to open and inspect the file before calling send_page with a response */
        if (con_info->is_err)
        {
            sprintf(ret_buffer, "{\"status\":\"fail\", \"msg\": \"%s\"}", con_info->err_msg);
            if (con_info->file_dir && con_info->file_dir[0] != '\0')
            {
                // unlink file if it exists
                printf("POST(X): unlink file '%s' due to: %s\n", con_info->file_dir, con_info->err_msg);
                unlink(con_info->file_dir);
            }
        }
        else
        {
            printf("POST: upload file %s with size: %ld\n", con_info->file_dir, con_info->upload_byte);
            add_FileNode(con_info->node);
            sprintf(ret_buffer, "{\"status\":\"ok\", \"code\": %d}", con_info->node.pwd);
        }

        return response_json(connection, ret_buffer, MHD_HTTP_OK);
    }
}

/*
 * Main entry for handling the web request
 * Returns: MHD_Result
 *
 */
static enum MHD_Result
ahc_echo(void *cls,
         struct MHD_Connection *connection,
         const char *url,
         const char *method,
         const char *version,
         const char *upload_data, size_t *upload_data_size, void **con_cls)
{
    char file_path[DIR_BUFFER_SIZE];
    char ret_buffer[STD_MSG_BUFFER_SIZE];

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
            5. /file/#pwd: response file with spec pwd
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
            sprintf(ret_buffer, "{\"file_max_byte\": %d, \"file_expire\": %d}", file_max_byte, file_expire);
            return response_json(connection, ret_buffer, MHD_HTTP_OK);
        }
        else if (strncmp(url, "/file", 5) == 0)
        {
            return response_file(connection, atoi(url + 6));
        }
        else
        {
            // invalid GET, reponse index.html to handle such situation
            debug("GET(X): %s", url);
            strcpy(file_path, "./index.html");
        }

        return response_resource(connection, file_path);
    }
    else if (strcmp(method, "POST") == 0)
    {
        /*
            Post request handler
            1. /upload: file uploading router
        */
        if (strcmp(url, "/upload") == 0)
        {
            return upload_req_handler(connection, upload_data, upload_data_size, con_cls);
        }

        // invalid POST, reponse index.html to handle such situation
        printf("POST(X): %s\n", file_path);
        strcpy(file_path, "./index.html");
        return response_resource(connection, "./index.html");
    }

    return MHD_NO; /* unexpected method */
}

/*
 * test case for hash collision
 *
 */
void test_HashCollision(int test_num)
{
    int collide_count = 0;

    // Initialize hashmap
    FileNode_hashmap = createHashmap(test_num);

    FileNode empty_filenode = create_FileNode("", 0);
    unsigned int pwd;

    for (int i = 0; i < test_num; i++)
    {
        pwd = generate_rand_6digit();
        collide_count += hashmap_insert(FileNode_hashmap, pwd, &empty_filenode);
    }

    printf("total insert: %d, collide: %d\n", test_num, collide_count);
    freeFileNodeList();
    freeHashmap(FileNode_hashmap);
}

int main(int argc, char *const *argv)
{

#ifdef TEST
    printf("!!! TEST MODE: to switch to the production mode, comment out the line '#define test 1'\n\n");
    if (argc != 2) {
        printf("usage: %s <test_num>\n", argv[0]);
        return 0;
    }
    test_HashCollision(atoi(argv[1]));
    return 0;
#endif

#ifdef DEBUG
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
        return 1;
    }

    // create storage_dir if not exist
    struct stat st = {0};

    if (stat(storage_dir, &st) == -1)
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
    FileNode_hashmap = createHashmap(max_file_count);

    // initialize old file node list
    deserialize_FileNodeList();

    // start server
    d = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
                         atoi(argv[1]), NULL, NULL,
                         &ahc_echo, NULL,
                         MHD_OPTION_NOTIFY_COMPLETED, request_completed,
                         NULL, MHD_OPTION_END);

    if (NULL == d)
    {
        perror("server start failed\n");
        return 1;
    }

    printf("* Server started at http://localhost:%s. Press enter to stop.\n\n", argv[1]);

    // Create the worker thread
    if (pthread_create(&tid, NULL, cleaner_worker, NULL) != 0)
    {
        perror("Error creating thread\n");
        return EXIT_FAILURE;
    }
    pthread_detach(tid);

    (void)getchar(); // Wait for user input to terminate the server

    terminate_handler(-1);
    return 0;
}
