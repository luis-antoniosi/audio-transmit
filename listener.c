#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;

    #define PRINT_SOCK_ERROR(msg) fprintf(stderr, "%s: %d\n", msg, WSAGetLastError())
    #define CLOSE_SOCKET(s) closesocket(s)
#elif __unix__
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    typedef int socket_t;

    #define INVALID_SOCKET (-1)
    #define PRINT_SOCK_ERROR(msg) perror(msg)
    #define CLOSE_SOCKET(s) close(s)
    #define SOCKET_ERROR (-1)
#endif

#define MINIAUDIO_IMPLEMENTATION
#include <stdio.h>
#include "miniaudio.h"

#define FORMAT ma_format_f32
#define CHANNELS 2
#define SAMPLE_RATE 48000
#define FRAMES_PER_PACKET 256
#define MAX_PACKET_BYTES 32768 // 4096 * 8

#define PORT "6969"

#define BUF_LEN 4096 

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    void *pBuffer;
    ma_uint32 framesToRead = frameCount;
    size_t bytesPerFrame = ma_get_bytes_per_frame(pDevice->playback.format, pDevice->playback.channels);

    if (ma_pcm_rb_acquire_read((ma_pcm_rb *)pDevice->pUserData, &framesToRead, &pBuffer) == MA_SUCCESS
        && framesToRead > 0)
    { 
        memcpy(pOutput, pBuffer, framesToRead * bytesPerFrame);

        if (framesToRead < frameCount)
            memset((char*)pOutput + (framesToRead * bytesPerFrame), 0, (frameCount - framesToRead) * bytesPerFrame); 

        ma_pcm_rb_commit_read((ma_pcm_rb *)pDevice->pUserData, framesToRead);
    }
    else
        memset(pOutput, 0, frameCount * bytesPerFrame);

    (void)pInput;
}

// void *get_in_addr(struct sockaddr *sa)
// {
//     if (sa->sa_family == AF_INET)
//         return &(((struct sockaddr_in*)sa)->sin_addr);

//     return &(((struct sockaddr_in6*)sa)->sin6_addr);
// }

int main()
{
    int returnCode = -1;

    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            fprintf(stderr, "WSAStartup failed.\n");
            return returnCode;
        }
    #endif

    socket_t sockfd = INVALID_SOCKET; 
    int numBytes = -1, rv = -1;
    struct addrinfo hints, *servInfo, *p;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servInfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        returnCode = -1;
        goto cleanup_info;
    }

    for (p = servInfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET)
        {
            PRINT_SOCK_ERROR("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == SOCKET_ERROR)
        {
            CLOSE_SOCKET(sockfd);
            PRINT_SOCK_ERROR("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "listener: failed to bind socket\n");
        returnCode = -1;
        goto cleanup_sock;
    }

    printf("Listening on port: %s\n", PORT);

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
    {
        fprintf(stderr, "listener: failed to initialize context.\n");
        returnCode = -1;
        goto cleanup_context;
    }

    ma_device_info* pPlaybackInfos = NULL;
    ma_uint32 playbackCount = -1;

    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, NULL, NULL) != MA_SUCCESS)
    {
        fprintf(stderr, "listener: failed to get context devices.\n");
        returnCode = -1;
        goto cleanup_context;
    }

    for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice++)
        printf("Playback: %d - %s\n", iDevice, pPlaybackInfos[iDevice].name);

    printf("Choose an id: ");
    int chosenID = -1;
    if ((scanf("%d", &chosenID) != 1) || chosenID < 0 || (ma_uint32)chosenID >= playbackCount)
    {
        fprintf(stderr, "listener: invalid device ID.\n");
        returnCode = -1;
        goto cleanup_context;
    }

    printf("Chosen: %d\n", chosenID);

    ma_pcm_rb rb;
    if (ma_pcm_rb_init(FORMAT, CHANNELS, SAMPLE_RATE, NULL, NULL, &rb) != MA_SUCCESS)
    {
        fprintf(stderr, "listener: failed to initialize ring buffer.\n");
        returnCode = -1;
        goto cleanup_rb;
    }  

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = FORMAT;
    config.playback.channels = CHANNELS;
    config.playback.pDeviceID = &pPlaybackInfos[chosenID].id;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = data_callback;
    config.pUserData = &rb;
    
    ma_device device;
    if (ma_device_init(&context, &config, &device) != MA_SUCCESS)
    {
        fprintf(stderr, "talker: failed to initialize playback device.\n");
        returnCode = -2;
        goto cleanup_device;
    }

    if (ma_device_start(&device) != MA_SUCCESS)
    {
        fprintf(stderr, "talker: failed to start playback device.\n");
        returnCode = -2;
        goto cleanup_device;
    }

    char recvBuf[MAX_PACKET_BYTES];
    size_t bytesPerFrame = ma_get_bytes_per_frame(FORMAT, CHANNELS);
    while (1)
    {
        struct sockaddr_storage addr;
        socklen_t addrLen = sizeof(addr);

        if ((numBytes = recvfrom(sockfd, recvBuf, sizeof(recvBuf), 0, (struct sockaddr *)&addr, &addrLen)) == SOCKET_ERROR)
        {
            PRINT_SOCK_ERROR("recvfrom");
            break;
        }

        ma_uint32 framesRemaining = (ma_uint32)(numBytes / bytesPerFrame);
        const char *srcPtr = recvBuf;

        while (framesRemaining > 0)
        {
            void *pBuffer;
            ma_uint32 framesToWrite = framesRemaining;

            if (ma_pcm_rb_acquire_write(&rb, &framesToWrite, &pBuffer) == MA_SUCCESS
                && framesToWrite > 0)
            {
                size_t bytesToCopy = framesToWrite * bytesPerFrame;
                memcpy(pBuffer, srcPtr, bytesToCopy);
                ma_pcm_rb_commit_write(&rb, framesToWrite);
                
                srcPtr += bytesToCopy;
                framesRemaining -= framesToWrite;
            }    
            else
                break;
        }
    }
    
    returnCode = 0;

    cleanup:
    cleanup_device:
        ma_device_uninit(&device);
    cleanup_rb:
        ma_pcm_rb_uninit(&rb);
    cleanup_context:
        ma_context_uninit(&context);
    cleanup_sock:
        CLOSE_SOCKET(sockfd);
    cleanup_info:
        if (servInfo)
            freeaddrinfo(servInfo); 

    #ifdef _WIN32
        WSACleanup();
    #endif

    return returnCode;
}
