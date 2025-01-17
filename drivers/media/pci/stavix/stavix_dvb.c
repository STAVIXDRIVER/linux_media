/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include "stavix.h"
#include "mxl58x.h"
#include "stid135.h"
#include "si2168.h"
#include "si2157.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static u32 GPIO_VALUE = 0xFFFFFF00; 

struct sec_priv {
	struct stavix_adapter *adap;
	int (*set_voltage)(struct dvb_frontend *fe,
			   enum fe_sec_voltage voltage);
};
static void stavix_spi_read(struct i2c_adapter *i2c,u8 reg, u32 *buf)
{	
	struct stavix_i2c *i2c_adap = i2c_get_adapdata(i2c);
	struct stavix_dev *dev = i2c_adap->dev;
	//*buf = pci_read(stavix_GPIO_BASE,reg );
	printk(KERN_INFO"spi_read\n");

	return ;
}
static void stavix_spi_write(struct i2c_adapter *i2c,u8 reg, u32 buf)
{	
	struct stavix_i2c *i2c_adap = i2c_get_adapdata(i2c);
	struct stavix_dev *dev = i2c_adap->dev;
	//pci_write(stavix_GPIO_BASE,reg,buf);
	printk(KERN_INFO"spi_write\n");
	return ;
}
static int stavix_set_voltage(struct dvb_frontend* fe,
		enum fe_sec_voltage voltage)
{
	struct sec_priv *priv = fe->sec_priv;
	struct stavix_gpio_config *cfg = &priv->adap->cfg->gpio;
	struct stavix_dev *dev = priv->adap->dev;

	dev_dbg(&dev->pci_dev->dev, "%s() %s\n", __func__,
		voltage == SEC_VOLTAGE_13 ? "SEC_VOLTAGE_13" :
		voltage == SEC_VOLTAGE_18 ? "SEC_VOLTAGE_18" :
		"SEC_VOLTAGE_OFF");

	switch (voltage) {
		case SEC_VOLTAGE_13:
			stavix_gpio_set_pin(dev, &cfg->lnb_power, 1);
			stavix_gpio_set_pin(dev, &cfg->lnb_voltage, 0);
			break;
		case SEC_VOLTAGE_18:
			stavix_gpio_set_pin(dev, &cfg->lnb_power, 1);
			stavix_gpio_set_pin(dev, &cfg->lnb_voltage, 1);
			break;
		default: 
			break;
	}

	if (priv->set_voltage)
		return priv->set_voltage(fe, voltage);
	else
		return 0;
}

static void stavix_release_sec(struct dvb_frontend* fe)
{
	struct sec_priv *priv;

	if (fe == NULL)
		return;

	priv = fe->sec_priv;
	if (priv == NULL)
		return;

	fe->ops.set_voltage = priv->set_voltage; 
	fe->sec_priv = NULL;
	kfree(priv);
}

static struct dvb_frontend *stavix_attach_sec(struct stavix_adapter *adap, struct dvb_frontend *fe)
{
	struct sec_priv *priv;

	priv = kzalloc(sizeof(struct sec_priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	priv->set_voltage = fe->ops.set_voltage;
	priv->adap = adap;

	fe->ops.set_voltage = stavix_set_voltage;
	fe->sec_priv = priv;

	return fe;
}

static int set_mac_address(struct stavix_adapter *adap) 
{
	struct stavix_dev *dev = adap->dev;

	struct i2c_adapter *i2c = &dev->i2c_bus.i2c_adap;
	u8 eep_addr[2]; 
	int ret;

	struct i2c_msg msg[] = {
		{ .addr = 0x50, .flags = 0,
		  .buf = eep_addr, .len = 2 },
		{ .addr = 0x50, .flags = I2C_M_RD,
		  .buf = adap->dvb_adapter.proposed_mac, .len = 6 }
	};
	eep_addr[0] = 0x00;
	if (dev->info->eeprom_addr)	
		eep_addr[1] = dev->info->eeprom_addr; 
	else
		eep_addr[1] = 0xa0;
	eep_addr[1] += 0x10 * adap->nr;
	ret = i2c_transfer(i2c, msg, 2);
	if (ret != 2) {
		dev_warn(&dev->pci_dev->dev,
			"error reading MAC address for adapter %d\n",
			adap->nr);
	} else {
		dev_info(&dev->pci_dev->dev,
			"MAC address %pM\n", adap->dvb_adapter.proposed_mac);
	}
	return 0;
};

static int start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct stavix_adapter *adapter = dvbdmx->priv;

	if (!adapter->feeds)
		sg_dma_enable(adapter);

	return ++adapter->feeds;
}

static int stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct stavix_adapter *adapter = dvbdmx->priv;

	if (--adapter->feeds)
		return adapter->feeds;

	sg_dma_disable(adapter);
	return 0;
}


static int max_set_voltage(struct i2c_adapter *i2c,
		enum fe_sec_voltage voltage, u8 rf_in)
{
	struct stavix_i2c *i2c_adap = i2c_get_adapdata(i2c);
	struct stavix_dev *dev = i2c_adap->dev;


	if (rf_in > 3)
		return -EINVAL;

	switch (voltage) {
	case SEC_VOLTAGE_13:
		GPIO_VALUE |= STAVIX_GPIO_PIN(rf_in, 0); 
		GPIO_VALUE &= ~STAVIX_GPIO_PIN(rf_in, 1);
		break;
	case SEC_VOLTAGE_18:
		GPIO_VALUE |= STAVIX_GPIO_PIN(rf_in, 0);
		GPIO_VALUE |= STAVIX_GPIO_PIN(rf_in, 1);
		break;
	case SEC_VOLTAGE_OFF:
	default:
		//GPIO_VALUE |= ~STAVIX_GPIO_PIN(rf_in, 0);
		break;
	}
	pci_write(STAVIX_GPIO_BASE, 0, GPIO_VALUE);
	return 0;
}



static struct mxl58x_cfg stavix_mxl58x_cfg = {
	.adr		= 0x60,
	.type		= 0x01,
	.clk		= 24000000,
	.cap		= 12,
	.fw_read	= NULL,

	.set_voltage	= max_set_voltage,
};

static struct stid135_cfg stavix_stid135_cfg = {
	.adr		= 0x68,
	.clk		= 25,
	.ts_mode	= TS_8SER,
	.set_voltage	= max_set_voltage,
	.write_properties = stavix_spi_write, 
	.read_properties = stavix_spi_read,
};

static int stavix_frontend_attach(struct stavix_adapter *adapter)
{
	struct stavix_dev *dev = adapter->dev;
	struct pci_dev *pci = dev->pci_dev;
	
	struct si2168_config si2168_config;
	struct si2157_config si2157_config;
	struct i2c_board_info info;
	struct i2c_client *client_demod, *client_tuner;

	struct i2c_adapter *i2c = &adapter->i2c->i2c_adap;

	adapter->fe = NULL;
	adapter->i2c_client_demod = NULL;
	adapter->i2c_client_tuner = NULL;

	set_mac_address(adapter);
	switch (pci->subsystem_vendor) {
	case 0x0610:
		
		adapter->fe = dvb_attach(mxl58x_attach, i2c,
				&stavix_mxl58x_cfg, adapter->nr);
		if (adapter->fe == NULL)
			goto frontend_atach_fail;
		if (stavix_attach_sec(adapter, adapter->fe) == NULL) {
			dev_warn(&dev->pci_dev->dev,
				"error attaching lnb control on adapter %d\n",
				adapter->nr);
		}
			
		break;
	case 0x0710:
			
		adapter->fe = dvb_attach(stid135_attach, i2c,
				&stavix_stid135_cfg, adapter->nr, adapter->nr/2);
		if (adapter->fe == NULL)
			goto frontend_atach_fail;
	
		break; 
			
	case 0x0810:
		/* attach demod */
		memset(&si2168_config, 0, sizeof(si2168_config));
		si2168_config.i2c_adapter = &i2c;
		si2168_config.fe = &adapter->fe;
		si2168_config.ts_mode = SI2168_TS_SERIAL;
		si2168_config.ts_clock_gapped = true;
		si2168_config.ts_clock_inv=0;

		memset(&info, 0, sizeof(struct i2c_board_info));
		strlcpy(info.type, "si2168", I2C_NAME_SIZE);
		info.addr = 0x64;
		info.platform_data = &si2168_config;
		request_module(info.type);
		client_demod = i2c_new_device(i2c, &info);
		if (client_demod == NULL ||
			client_demod->dev.driver == NULL)
		    goto frontend_atach_fail;
		if (!try_module_get(client_demod->dev.driver->owner)) {
		    i2c_unregister_device(client_demod);
		    goto frontend_atach_fail;
		}
		adapter->i2c_client_demod = client_demod;

		/* attach tuner */
		memset(&si2157_config, 0, sizeof(si2157_config));
		si2157_config.fe = adapter->fe;
		si2157_config.if_port = 1;

		memset(&info, 0, sizeof(struct i2c_board_info));
		strlcpy(info.type, "si2157", I2C_NAME_SIZE);
		info.addr = 0x60;
		info.platform_data = &si2157_config;
		request_module(info.type);
		client_tuner = i2c_new_device(i2c, &info);
		if (client_tuner == NULL ||
			client_tuner->dev.driver == NULL)
		    goto frontend_atach_fail;

		if (!try_module_get(client_tuner->dev.driver->owner)) {
		    i2c_unregister_device(client_tuner);
		    goto frontend_atach_fail;
		}
		adapter->i2c_client_tuner = client_tuner;
		break;
			
	default:
		dev_warn(&dev->pci_dev->dev, "unknonw card\n");
		return -ENODEV;
		break;
	}
	strlcpy(adapter->fe->ops.info.name,stavix_boards[pci->subsystem_vendor].name,52);
	return 0;

frontend_atach_fail:
	//tbsecp3_i2c_remove_clients(adapter);
	if (adapter->fe != NULL)
		dvb_frontend_detach(adapter->fe);
	adapter->fe = NULL;
	dev_err(&dev->pci_dev->dev, "stavix frontend %d attach failed\n",
		adapter->nr);

	return -ENODEV;
}

int stavix_dvb_init(struct stavix_adapter *adapter)
{
	struct stavix_dev *dev = adapter->dev;
	struct dvb_adapter *adap = &adapter->dvb_adapter;
	struct dvb_demux *dvbdemux = &adapter->demux;
	struct dmxdev *dmxdev;
	struct dmx_frontend *fe_hw;
	struct dmx_frontend *fe_mem;
	int ret;

	ret = dvb_register_adapter(adap, "stavix DVB Adapter",
					THIS_MODULE,
					&adapter->dev->pci_dev->dev,
					adapter_nr);
	if (ret < 0) {
		dev_err(&dev->pci_dev->dev, "error registering adapter\n");
		if (ret == -ENFILE)
			dev_err(&dev->pci_dev->dev,
				"increase DVB_MAX_ADAPTERS (%d)\n",
				DVB_MAX_ADAPTERS);
		return ret;
	}

	adap->priv = adapter;
	dvbdemux->priv = adapter;
	dvbdemux->filternum = 256;
	dvbdemux->feednum = 256;
	dvbdemux->start_feed = start_feed;
	dvbdemux->stop_feed = stop_feed;
	dvbdemux->write_to_decoder = NULL;
	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING |
				      DMX_SECTION_FILTERING |
				      DMX_MEMORY_BASED_FILTERING);

	ret = dvb_dmx_init(dvbdemux);
	if (ret < 0) {
		dev_err(&dev->pci_dev->dev, "dvb_dmx_init failed\n");
		goto err0;
	}

	dmxdev = &adapter->dmxdev;

	dmxdev->filternum = 256;
	dmxdev->demux = &dvbdemux->dmx;
	dmxdev->capabilities = 0;

	ret = dvb_dmxdev_init(dmxdev, adap);
	if (ret < 0) {
		dev_err(&dev->pci_dev->dev, "dvb_dmxdev_init failed\n");
		goto err1;
	}

	fe_hw = &adapter->fe_hw;
	fe_mem = &adapter->fe_mem;

	fe_hw->source = DMX_FRONTEND_0;
	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, fe_hw);
	if ( ret < 0) {
		dev_err(&dev->pci_dev->dev, "dvb_dmx_init failed");
		goto err2;
	}

	fe_mem->source = DMX_MEMORY_FE;
	ret = dvbdemux->dmx.add_frontend(&dvbdemux->dmx, fe_mem);
	if (ret  < 0) {
		dev_err(&dev->pci_dev->dev, "dvb_dmx_init failed");
		goto err3;
	}

	ret = dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, fe_hw);
	if (ret < 0) {
		dev_err(&dev->pci_dev->dev, "dvb_dmx_init failed");
		goto err4;
	}

	ret = dvb_net_init(adap, &adapter->dvbnet, adapter->dmxdev.demux);
	if (ret < 0) {
		dev_err(&dev->pci_dev->dev, "dvb_net_init failed");
		goto err5;
	}
	
	stavix_frontend_attach(adapter);
	if (adapter->fe == NULL) {
		dev_err(&dev->pci_dev->dev, "frontend attach failed\n");
		ret = -ENODEV;
		goto err6;
	}

	ret = dvb_register_frontend(adap, adapter->fe);
	if (ret < 0) {
		dev_err(&dev->pci_dev->dev, "frontend register failed\n");
		goto err7;
	}

	return ret;

err7:
	dvb_frontend_detach(adapter->fe);
err6:
	stavix_release_sec(adapter->fe);

	dvb_net_release(&adapter->dvbnet);
err5:
	dvbdemux->dmx.close(&dvbdemux->dmx);
err4:
	dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, fe_mem);
err3:
	dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, fe_hw);
err2:
	dvb_dmxdev_release(dmxdev);
err1:
	dvb_dmx_release(dvbdemux);
err0:
	dvb_unregister_adapter(adap);
	return ret;
}

void stavix_dvb_exit(struct stavix_adapter *adapter)
{
	struct dvb_adapter *adap = &adapter->dvb_adapter;
	struct dvb_demux *dvbdemux = &adapter->demux;

	if (adapter->fe) {
		dvb_unregister_frontend(adapter->fe);
		stavix_release_sec(adapter->fe);
		dvb_frontend_detach(adapter->fe);
		adapter->fe = NULL;

	}
	dvb_net_release(&adapter->dvbnet);
	dvbdemux->dmx.close(&dvbdemux->dmx);
	dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &adapter->fe_mem);
	dvbdemux->dmx.remove_frontend(&dvbdemux->dmx, &adapter->fe_hw);
	dvb_dmxdev_release(&adapter->dmxdev);
	dvb_dmx_release(&adapter->demux);
	dvb_unregister_adapter(adap);
}
