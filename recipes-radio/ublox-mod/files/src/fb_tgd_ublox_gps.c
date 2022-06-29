/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * GPS I2C client driver as a 'misc' driver
 */
#include "fb_tgd_ublox_gps.h"

#include <linux/of_device.h>
#include <linux/of.h>

#define TG_UBLOX_OF_DEVICE "facebook," TGD_UBLOX_GPS_DEV_NAME
#define UBLOX_I2C_DATA_LEN_REG 0xFD

#define MRVL_I2C_OFFLOAD_THR 8
#define USE_MRVL_I2C_TRANS_GEN

static int tgd_ublox_dev_data_len(struct i2c_client *client);

//------------- I2C bus registering data ---------------------------
static int tgd_ublox_gps_probe(struct i2c_client *client,
			       const struct i2c_device_id *id);
static int tgd_ublox_gps_remove(struct i2c_client *client);
static void tgd_ublox_gps_shutdown(struct i2c_client *client);

/**************************************************************************
 * Send the given message to the UBLOX through the I2C interface
 * Invoked from the ublox_msg_handler,
 * dev_hndl should be the value given during the initialization
 **************************************************************************/
int ublox_i2c_send(t_ublox_hndlr dev_hndl, unsigned char *tx_msg, int len)
{
	int ret_stat;
	struct i2c_client *client;
	struct tgd_ublox_gps_prv_data *prv_data;

	prv_data = (struct tgd_ublox_gps_prv_data *)dev_hndl;
	client = prv_data->client;
	ret_stat = i2c_master_send(client, tx_msg, len);
	if (ret_stat < 0) {
		dev_err(&client->dev, "i2c_master_send error: %d\n", ret_stat);
		prv_data->stats.tx_error_count++;
		return -1;
	} else {
		prv_data->stats.tx_byte_count += len;
		prv_data->stats.tx_pkt_count++;
	}

	return 0;
}

/****************************************************************************/
int ublox_i2c_receive(t_ublox_hndlr dev_hndl, unsigned char *buf,
		      int buf_max_size)
{
	s32 gps_buf_size = 0, buf_size = 0;
	int rx_count = 0;
	int rd_req_size = 0;
	struct i2c_client *client;
	struct tgd_ublox_gps_prv_data *prv_data;

	prv_data = (struct tgd_ublox_gps_prv_data *)dev_hndl;
	client = prv_data->client;
	prv_data->stats.rx_poll_count++;
	gps_buf_size = tgd_ublox_dev_data_len(client);
	if (gps_buf_size <= 0) {
		prv_data->stats.rx_len_zero_count++;
		return 0;
	}
	if (gps_buf_size > buf_max_size) {
		dev_err(&client->dev, "Ublox RxLen: %d > MaxSize: %d\n",
			gps_buf_size, buf_max_size);
		gps_buf_size = buf_max_size;
		prv_data->stats.rx_len_truncated_count++;
	}
	memset(buf, 0, gps_buf_size);

	rx_count = 0;

	/* Check the FIFO empty case by reading only the first byte */
	buf_size = i2c_master_recv(client, (char *)buf, 1);
	if (buf_size != 1) {
		prv_data->stats.rx_error_count++;
		dev_err(&client->dev, "I2C RdReqLen:1 != RetLen:%d\n",
			buf_size);
		goto end;
	}

	if ((*buf) == 0xFF) {
		// The first byte read should not be 0xFF, case we see repeated
		// FFs
		prv_data->stats.rx_fifo_empty_count++;
		rx_count = -1;
		goto end;
	}
	rx_count = 1;
	rd_req_size = gps_buf_size - 1;
	do {
#ifdef USE_MRVL_I2C_TRANS_GEN
		/* For armada I2C transaction logic, length should be <= 8
		 * use transaction logic to reduce software/interrupt overhead
		 * Do a segmented read of max size of 8 bytes
		 */
		rd_req_size =
		    ((gps_buf_size - rx_count) >= MRVL_I2C_OFFLOAD_THR)
			? MRVL_I2C_OFFLOAD_THR
			: gps_buf_size - rx_count;
#endif
		buf_size = i2c_master_recv(client, (char *)(buf + rx_count),
					   rd_req_size);
		if (buf_size != rd_req_size) {
			prv_data->stats.rx_error_count++;
			dev_err(&client->dev, "I2C RdReqLen:%d != RetLen:%d\n",
				rd_req_size, buf_size);
			goto end;
		}
		rx_count += buf_size;

	} while (rx_count < gps_buf_size);
	if (rx_count)
		prv_data->stats.rx_pkt_count++;

end:
	return rx_count;
}

/**************************************************************************/
static int tgd_ublox_dev_data_len(struct i2c_client *client)
{
	int gps_buflen = 0;

	gps_buflen = i2c_smbus_read_word_data(client, UBLOX_I2C_DATA_LEN_REG);
	if (gps_buflen < 0) {
		dev_err(&client->dev, "couldn't read register(%#x) from GPS.\n",
			UBLOX_I2C_DATA_LEN_REG);
		return 0;
	}
	/* 16 bit length info read 0xfd  */
	gps_buflen =
	    (((gps_buflen << 8) & 0xFF00) + ((gps_buflen >> 8) & 0xFF));
	return gps_buflen;
}

/*********************************************************************
*********************************************************************/
static int tgd_ublox_gps_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct tgd_ublox_gps_prv_data *prv_data;
	struct device *dev = &client->dev;
	int ret = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality failed\n");
		ret = -ENODEV;
		goto err_out;
	}

	/* allocate driver private ds */
	prv_data = devm_kzalloc(dev, sizeof(struct tgd_ublox_gps_prv_data),
				GFP_KERNEL);
	if (prv_data == NULL) {
		dev_err(&client->dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_out;
	}

	i2c_set_clientdata(client, prv_data);
	prv_data->client = client;

	prv_data->msg_handler = tgd_ublox_msg_handler_init(prv_data);
	if (prv_data->msg_handler == NULL) {
		ret = -ENODEV;
		goto err_out;
	}
	dev_info(&client->dev, "Terragraph UBLOX GPS driver initialized\n");
	return 0;
err_out:
	dev_err(&client->dev,
		"Terragraph UBLOX GPS driver failed to attach, error %d\n",
		ret);
	return ret;
}

/***********************************************************************
 * Invoked by I2C core with unregistering the ublox
 ***********************************************************************/
static int tgd_ublox_gps_remove(struct i2c_client *client)
{
	struct tgd_ublox_gps_prv_data *prv_data;
	struct device *dev = &client->dev;

	prv_data = i2c_get_clientdata(client);
	if (prv_data->msg_handler != NULL)
		tgd_ublox_msg_handler_deinit(prv_data->msg_handler);
	devm_kfree(dev, prv_data);
	return 0;
}

static void tgd_ublox_gps_shutdown(struct i2c_client *client)
{
	struct tgd_ublox_gps_prv_data *prv_data;

	prv_data = i2c_get_clientdata(client);

	dev_err(&client->dev, "shutting down\n");

	if (prv_data->msg_handler != NULL)
		tgd_ublox_msg_handler_deinit(prv_data->msg_handler);
	prv_data->msg_handler = NULL;
}
/***********************************************************************
 * Stat collection for the I2C continuous
 ***********************************************************************/
int tgd_get_i2c_stat(t_ublox_hndlr dev_hndl, char *buf, int buf_size)
{
	struct tgd_ublox_gps_prv_data *prv_data;
	int gsv_len = 0;

	prv_data = (struct tgd_ublox_gps_prv_data *)dev_hndl;

	gsv_len = scnprintf(buf, buf_size, "\n======== I2C Stats ======\n");
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
			     "RX_poll_count", prv_data->stats.rx_poll_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
			     "RX_pkt_count", prv_data->stats.rx_pkt_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
			     "RX_len_zero", prv_data->stats.rx_len_zero_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len +=
	    scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
		      "RX_fifo_empty", prv_data->stats.rx_fifo_empty_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
			     "RX_len_truncated",
			     prv_data->stats.rx_len_truncated_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len +=
	    scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
		      "RX_loop_break", prv_data->stats.rx_loop_break_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
			     "RX_Rd_Error", prv_data->stats.rx_error_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
			     "TX_pkt_count", prv_data->stats.tx_pkt_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
			     "TX_byte_count", prv_data->stats.tx_byte_count);
	if (gsv_len >= buf_size)
		goto exit_1;

	gsv_len += scnprintf(buf + gsv_len, buf_size - gsv_len, "%-20s: %d\n",
			     "TX_error_count", prv_data->stats.tx_error_count);
	if (gsv_len >= buf_size)
		goto exit_1;

exit_1:
	return gsv_len;
}

/*
 * Linux module registration
 */

#ifdef CONFIG_OF
static struct of_device_id tgd_ublox_ids[] = {
    {.compatible = TG_UBLOX_OF_DEVICE}, {}};
MODULE_DEVICE_TABLE(of, tgd_ublox_ids);
#endif

static const struct i2c_device_id tgd_ublox_gps_id_table[] = {
    {TGD_UBLOX_GPS_DEV_NAME, 0}, {}};

MODULE_DEVICE_TABLE(i2c, tgd_ublox_gps_id_table);

static struct i2c_driver tgd_ublox_gps_driver = {
    .driver = {.owner = THIS_MODULE, .name = TGD_UBLOX_GPS_DEV_NAME},
    .id_table = tgd_ublox_gps_id_table,
    .probe = tgd_ublox_gps_probe,
    .remove = tgd_ublox_gps_remove,
    .shutdown = tgd_ublox_gps_shutdown,
};

module_i2c_driver(tgd_ublox_gps_driver);

MODULE_DESCRIPTION("Facebook GPS Driver for U-Blox");
MODULE_AUTHOR("Joseph Kizhakkeparampil");
MODULE_LICENSE("Dual MIT/GPL");
