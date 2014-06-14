#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <signal.h>
#include <assert.h>

#include "linux/video/cec.h"

#define OPCODE_FEATURE_ABORT            0x00
#define OPCODE_STANDBY                  0x36
#define OPCODE_GIVE_OSD_NAME            0x46
#define OPCODE_SET_OSD_NAME             0x47
#define OPCODE_ACTIVE_SOURCE            0x82
#define OPCODE_DEVICE_VENDOR_ID         0x87
#define OPCODE_VENDOR_COMMAND           0x89
#define OPCODE_GIVE_DEVICE_VENDOR_ID    0x8C
#define OPCODE_GIVE_DEVICE_POWER_STATUS 0x8F
#define OPCODE_GET_CEC_VERSION          0x9E
#define OPCODE_GET_MENU_LANGUAGE        0x91

#define ABORT_REASON_UNRECOGNIZED_OPCODE 0x0
#define ABORT_REASON_WRONG_MODE          0x1
#define ABORT_REASON_CANNOT_PROVIDE_SRC  0x2
#define ABORT_REASON_INVALID_OPERAND     0x3
#define ABORT_REASON_REFUSED             0x4

static const char* ABORT_REASON[] = {
    "Unrecognized opcode",
    "Not in correct mode to respond",
    "Cannot provide source",
    "Invalid operand",
    "Refused"
};

#define die(str, args...) do { \
    perror(str); \
    exit(EXIT_FAILURE); \
} while(0)

struct cec_ctx {
    int fd;
    struct cec_dev dev;
};

static void signal_handler(int signo)
{
    printf("\nCaught SIGINT\n");
    //close(fd);
    exit(EXIT_SUCCESS);
}

static int dump_rx_cmd(struct cec_rx_data* rx)
{
    int i;
    char buf[64];
    char *p = buf;

    if (rx->rx_count) {
        for (i = 0; i < rx->rx_count; i++) {
            p += snprintf(p, sizeof(buf) - (p - buf), "0x%02X, ", rx->rx_operand[i]);
        }
        *(p - 2) = '\0';
    } else {
        *p = '\0';
    }

    printf("RX <- {initiator:0x%02X, dest:0x%02X, opcode:0x%02X, len:%02d, data:[%s]}\n",
            rx->init_device_id, rx->dest_device_id, rx->rx_cmd, rx->rx_count, buf);

    return 0;
}

static int dump_tx_cmd(struct cec_tx_data* tx)
{
    int i;
    char buf[64];
    char *p = buf;

    if (tx->tx_count) {
        for (i = 0; i < tx->tx_count; i++) {
            p += snprintf(p, sizeof(buf) - (p - buf), "0x%02X, ", tx->tx_operand[i]);
        }
        *(p - 2) = '\0';
    } else {
        *p = '\0';
    }

    printf("TX -> {initiator:0x%02X, dest:0x%02X, opcode:0x%02X, len:%02d, data:[%s]}\n",
            tx->initiator_device_id, tx->dest_device_id, tx->tx_cmd, tx->tx_count, buf);

    return 0;
}

/**
 * @ret 1 on success
 */
static int cmd_tx(const struct cec_ctx* ctx, struct cec_tx_data* tx)
{
    int ret = ioctl(ctx->fd, CEC_TRANSMIT_CMD, tx);

    if (ret != 1) {
        printf("TX result = %d\n", ret);
    } else {
        dump_tx_cmd(tx);
    }

    return ret;
}

/**
 * @ret 1 on success
 */
static int ping(const struct cec_ctx* ctx, int dest_id)
{
    int ret;
    struct cec_tx_data tx_cmd;
    memset(&tx_cmd, 0, sizeof(tx_cmd));

    tx_cmd.dest_device_id = dest_id;
    tx_cmd.initiator_device_id = ctx->dev.device_id;
    tx_cmd.send_ping = 0x1;
    tx_cmd.retry_count = 0x5;

    ret = cmd_tx(ctx, &tx_cmd);

    printf("PING result = %s\n", ret == 1 ? "SUCCESS" : "ERROR" );

    return ret;
}

/**
 * @ret 0 on success
 */
static int cmd_rx(const struct cec_ctx* ctx, struct cec_rx_data* rx)
{
    int ret = ioctl(ctx->fd, CEC_RECV_CMD, rx);

    //printf("RECV result = %s\n", ret == 0 ? "SUCCESS" : "ERROR" );

    return ret;
}

static int get_simple_cmd(const struct cec_ctx* ctx, int dest_id, int opcode)
{
    int ret;
    struct cec_tx_data tx_cmd;
    memset(&tx_cmd, 0, sizeof(tx_cmd));

    tx_cmd.dest_device_id = dest_id;
    tx_cmd.initiator_device_id = ctx->dev.device_id;
    tx_cmd.retry_count = 0x5;
    tx_cmd.tx_cmd = opcode;
    tx_cmd.tx_count = 0x0;

    ret = cmd_tx(ctx, &tx_cmd);

    return ret;
}

static int send_feature_abort(const struct cec_ctx* ctx, int dest_id, int opcode, int reason)
{
    int ret;
    struct cec_tx_data tx_cmd;
    memset(&tx_cmd, 0, sizeof(tx_cmd));

    tx_cmd.dest_device_id = dest_id;
    tx_cmd.initiator_device_id = ctx->dev.device_id;
    tx_cmd.retry_count = 0x5;
    tx_cmd.tx_cmd = OPCODE_FEATURE_ABORT;
    tx_cmd.tx_count = 0x2;
    tx_cmd.tx_operand[0] = opcode;
    tx_cmd.tx_operand[1] = reason;

    ret = cmd_tx(ctx, &tx_cmd);

    return ret;
}

static int rx_handler(struct cec_ctx* ctx)
{
    int ret = 0;
    struct cec_rx_data rx_cmd;
    struct cec_tx_data txd;
    memset(&rx_cmd, 0, sizeof(rx_cmd));
    memset(&txd, 0, sizeof(txd));

    ret = cmd_rx(ctx, &rx_cmd);
    if (!ret) {
        switch (rx_cmd.rx_cmd) {

            case OPCODE_FEATURE_ABORT:
                {
                    int reason;
                    assert(rx_cmd.rx_count == 2);
                    reason = rx_cmd.rx_operand[1];
                    printf("FEATURE_ABORT opcode:0x%02X, reason:0x%02X (%s)\n",
                            rx_cmd.rx_operand[0], reason, ABORT_REASON[reason]);
                }
                break;
                
            case OPCODE_GIVE_DEVICE_VENDOR_ID:
                {
#if 0
                    /* If received via broadcast, return via broadcast */
                    if (rx_cmd.dest_device_id == 0xf)
                        txd.dest_device_id = rx_cmd.init_device_id;
                    else
                        txd.dest_device_id = rx_cmd.init_device_id;

                    txd.initiator_device_id = ctx->dev.device_id;
                    txd.retry_count = 5;
                    txd.tx_cmd = OPCODE_DEVICE_VENDOR_ID;
                    txd.tx_count = 3;
                    txd.tx_operand[0] = 0x11;
                    txd.tx_operand[1] = 0x22;
                    txd.tx_operand[2] = 0x33;

                    ret = cmd_tx(ctx, &txd);
#endif
                    ret = send_feature_abort(ctx,
                            rx_cmd.init_device_id,
                            rx_cmd.rx_cmd,
                            ABORT_REASON_UNRECOGNIZED_OPCODE);
                }
                break;

            case OPCODE_DEVICE_VENDOR_ID:
            case OPCODE_VENDOR_COMMAND:
                /* Ignore these */
                break;

            default:
                dump_rx_cmd(&rx_cmd);
        }
    }
    return ret;
}


/**
 * Main
 */
int main(void)
{
    int ret = 0;
    struct cec_ctx ctx;
    struct cec_rx_data rx_cmd;
    struct cec_tx_data tx_cmd;
    int cnt = 0;

    memset(&ctx.dev, 0, sizeof(ctx.dev));
    memset(&rx_cmd, 0, sizeof(rx_cmd));
    memset(&tx_cmd, 0, sizeof(tx_cmd));

    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        printf("error registering signal handler\n");
        exit(EXIT_FAILURE);
    }

    ctx.dev.clear_existing_device = 0x1;
    ctx.dev.device_id = 0x3;

    ctx.fd = open("/dev/cec", O_RDWR);
    if (ctx.fd < 0)
        die("error: open");

    /**
     * Register the device
     */
    if (ioctl(ctx.fd, CEC_REGISTER_DEVICE, &ctx.dev) < 0)
        die("error: ioctl CEC_REGISTER_DEVICE");

    printf("HDMI device registered\n");

    /**
     * Register the device
     */
    if (ioctl(ctx.fd, CEC_GET_PHY_ADDR, &ctx.dev.phy_addr) < 0)
        die("error: ioctl CEC_GET_PHY_ADDR");

    printf("HDMI CEC phy_addr = 0x%x\n", ctx.dev.phy_addr);

    /**
     * Send a ping to the TV (0x0)
     */
    ping(&ctx, 0x0);

    //get_cec_version(&ctx, 0x0);
    //give_osd_name(&ctx, 0x0);
    get_simple_cmd(&ctx, 0x0, OPCODE_GIVE_DEVICE_POWER_STATUS);
   
    struct cec_tx_data txd;
   
#if 0 
    /** 
     * Attempt to put the TV in to standby
     */
    memset(&txd, 0, sizeof(txd));
    txd.dest_device_id = 0x0; /* standard says broadcast */
    txd.initiator_device_id = ctx.dev.device_id;
    txd.retry_count = 5;
    txd.tx_cmd = OPCODE_STANDBY;
    txd.tx_count = 0;

    ret = cmd_tx(&ctx, &txd);
#endif


#if 1
    /** 
     * Image View On
     */
    memset(&txd, 0, sizeof(txd));
    txd.dest_device_id = 0x0; /* standard says broadcast */
    txd.initiator_device_id = ctx.dev.device_id;
    txd.retry_count = 5;
    txd.tx_cmd = 0x04; /* IMAGE VIEW ON */
    txd.tx_count = 0;

    ret = cmd_tx(&ctx, &txd);
#endif


#if 1
    /**
     * Active source
     */
    memset(&txd, 0, sizeof(txd));
    txd.dest_device_id = 0xf; /* standard says broadcast */
    txd.initiator_device_id = ctx.dev.device_id;
    txd.retry_count = 5;
    txd.tx_cmd = OPCODE_ACTIVE_SOURCE;
    txd.tx_count = 2;
    txd.tx_operand[0] = ((char*)&ctx.dev.phy_addr)[0];
    txd.tx_operand[1] = ((char*)&ctx.dev.phy_addr)[1];
    //txd.tx_operand[2] = ((char*)&ctx.dev.phy_addr)[2];
    //txd.tx_operand[3] = ((char*)&ctx.dev.phy_addr)[3];

    ret = cmd_tx(&ctx, &txd);
#endif


    while (1) {
        rx_handler(&ctx);
        usleep(100000);
    }


    close(ctx.fd);

    return 0;
}

