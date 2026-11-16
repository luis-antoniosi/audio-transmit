#define MINIAUDIO_IMPLEMENTATION

#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "miniaudio.h"

#define FORMAT ma_format_f32
#define CHANNELS 2
#define SAMPLE_RATE 48000
#define FRAMES_PER_PACKET 256

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

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main()
{
    int sockfd = -1, numBytes = -1, rv = -1;
    struct addrinfo hints, *servInfo, *p;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servInfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servInfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "listener: failed to bind socket\n");
        return 1;
    }

    freeaddrinfo(servInfo);

    char buf[BUF_LEN];
    printf("Listening on port: %s\n", PORT);

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
        return -1;

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;

    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, NULL, NULL) != MA_SUCCESS)
        return -1;

    for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice++)
        printf("Playback: %d - %s\n", iDevice, pPlaybackInfos[iDevice].name);

    printf("Choose an id: ");
    int chosenID = -1;
    scanf("%d", &chosenID);

    if (chosenID < 0 || chosenID >= playbackCount)
        return -1;

    ma_pcm_rb rb;
    if (ma_pcm_rb_init(FORMAT, CHANNELS, SAMPLE_RATE, NULL, NULL, &rb) != MA_SUCCESS)
        return -1;

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
        fprintf(stderr, "Failed to initialize playback device.\n");
        return -2;
    }

    if (ma_device_start(&device) != MA_SUCCESS)
    {
        ma_device_uninit(&device);
        fprintf(stderr, "Failed to start playback device.\n");
        return -2;
    }

    while (1)
    {
        void *pBuffer;
        ma_uint32 framesToWrite = FRAMES_PER_PACKET;

        if (ma_pcm_rb_acquire_write(&rb, &framesToWrite, &pBuffer) == MA_SUCCESS
            && framesToWrite > 0)
        {
            size_t maxBytes = framesToWrite * ma_get_bytes_per_frame(FORMAT, CHANNELS);

            struct sockaddr_storage addr;
            socklen_t addr_len = sizeof(addr);
            if ((numBytes = recvfrom(sockfd, pBuffer, maxBytes, 0, (struct sockaddr *)&addr, &addr_len)) == -1)
            {
                perror("recvfrom");
                ma_pcm_rb_commit_write(&rb, 0);
                break;
            }

            ma_uint32 framesReceived = numBytes / ma_get_bytes_per_frame(FORMAT, CHANNELS);
            ma_pcm_rb_commit_write(&rb, framesReceived);
        }    
    }
    
    ma_context_uninit(&context);
    ma_device_uninit(&device);
    ma_pcm_rb_uninit(&rb);
    close(sockfd);

    return 0;
}
