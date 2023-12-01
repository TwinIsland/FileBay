#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>

// comment in out in product environment
#define debug 1

#define CONFIG_NUM_EXPECT 4

static int thread_should_stop = 0;
static pthread_t tid;
static struct MHD_Daemon *d;
static char *config_file = "CONFIG";

// Config parameter
static long file_max_byte, file_expire, worker_period_minute;
static char storage_dir[128];

struct ConnectionInfo
{
    FILE *fp;
    size_t total_size;
    struct MHD_PostProcessor *post_processor;
};

/*
 * Initialize config parameter
 * Returns: 1 if something goes wrong
 *
 */
int config_initialize()
{
    FILE *file = fopen(config_file, "r");
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
        else if (sscanf(line, "worker_period_minute:%ld", &worker_period_minute) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "storage_dir:%s", storage_dir) == 1)
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
    struct timespec ts;

    while (!thread_should_stop)
    {

#ifdef debug
        printf("(DEBUG) cleaner worker wake up\n");
        sleep(1);
        printf("(DEBUG) cleaner thread sleep\n");
        sleep(10);
        goto while_end;
#endif

        sleep(worker_period_minute * 60);

        // Open the directory
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
                printf("Deleted file: %s\n", file_path);
            }
            else
            {
                perror("Error deleting file");
            }
        }

        // Close the directory
        closedir(dir);
    while_end:
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

static enum MHD_Result get_req_handler(struct MHD_Connection *connection, char *file_path)
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
                             &file_size);

    response = MHD_create_response_from_buffer(file_size,
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

static enum MHD_Result iterate_post(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
                                    const char *filename, const char *content_type,
                                    const char *transfer_encoding, const char *data,
                                    uint64_t off, size_t size)
{

    struct ConnectionInfo *con_info = coninfo_cls;

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

    printf("POST: %zu bytes\n", *upload_data_size);

    if (NULL == *con_cls)
    {
        // first time post, create ConnectionInfo
        struct ConnectionInfo *con_info = malloc(sizeof(struct ConnectionInfo));
        if (NULL == con_info)
        {
            return MHD_NO;
        }
        con_info->fp = NULL;
        con_info->total_size = 0;
        con_info->post_processor = MHD_create_post_processor(connection, 64 * 1024, &iterate_post, con_info);

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
            // Incoming data exceeds file size limit
            return MHD_NO;
        }

        MHD_post_process(con_info->post_processor, upload_data, *upload_data_size);
        con_info->total_size += *upload_data_size;
        *upload_data_size = 0;
        return MHD_YES;
    }
    else
    {
        // request complete, do some cleaning
        struct ConnectionInfo *con_info = *con_cls;
        if (NULL == con_info)
            return MHD_NO;

        // Send response indicating upload is complete
        const char *page = "<html><body>File uploaded successfully.</body></html>";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(page), (void *)page, MHD_RESPMEM_PERSISTENT);
        MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        if (con_info->fp)
        {
            fclose(con_info->fp);
        }

        if (con_info->post_processor)
        {
            MHD_destroy_post_processor(con_info->post_processor);
        }

        free(con_info);
        *con_cls = NULL;
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
        else
        {
            // invalid GET, reponse index.html to handle such situation
            printf("GET(X): %s\n", url);
            strcpy(file_path, "./index.html");
        }

        return get_req_handler(connection, file_path);
    }
    else if (strcmp(method, "POST") == 0)
    {
        /*
            Post request handler
            1. /upload: file uploading router
        */
        if (strcmp(url, "/") == 0)
        {
            return upload_req_handler(connection, upload_data, upload_data_size, con_cls);
        }

        // invalid POST, reponse index.html to handle such situation
        printf("POST(X): %s\n", file_path);
        strcpy(file_path, "./index.html");
        return get_req_handler(connection, "./index.html");
    }

    return MHD_NO; /* unexpected method */
}

int main(int argc, char *const *argv)
{
    // signal handling
    signal(SIGINT, terminate_handler);
    signal(SIGTERM, terminate_handler);

    if (argc != 2)
    {
        fprintf(stderr, "%s PORT\n", argv[0]);
        return 1;
    }

#ifdef debug
    printf("!!! DEBUG MODE: to switch to the production mode, comment out the line '#define debug 1'\n\n");
#endif

    // initialize global config parameter
    if (config_initialize() == 1)
    {
        fprintf(stderr, "config file load error\n");
        return 1;
    }

    // start server
    d = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
                         atoi(argv[1]), NULL, NULL,
                         &ahc_echo, NULL,
                         MHD_OPTION_END);

    if (NULL == d)
    {
        fprintf(stderr, "server start failed\n");
        return 1;
    }

    printf("* Server started. Press enter to stop.\n\n");

    // Create the worker thread
    if (pthread_create(&tid, NULL, cleaner_worker, NULL) != 0)
    {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }

    (void)getchar(); // Wait for user input to terminate the server

    terminate_handler(-1);
    return 0;
}
