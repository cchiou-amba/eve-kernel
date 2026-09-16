struct regmap *G_SGMII_REGMAP = NULL;
#define PLL_RST_MASK 				_mask(4,1)
#define _mask(lsb,sz)				(((1<<(sz))-1)<<(lsb))
#define _set_mask(data,lsb,sz,val)		(((data)&(~_mask(lsb,sz)))|((val)<<(lsb)))

#define SGMII_PHY_DEBUG_ADDRESS (0)
#define SGMIITX0_CTR0				(SGMII_PHY_DEBUG_ADDRESS+0x00)
#define SGMIITX0_CTR1				(SGMII_PHY_DEBUG_ADDRESS+0x04)
#define SGMIITX0_OBSV0				(SGMII_PHY_DEBUG_ADDRESS+0x08)
#define SGMIITX1_CTR0				(SGMII_PHY_DEBUG_ADDRESS+0x0c)
#define SGMIITX1_CTR1				(SGMII_PHY_DEBUG_ADDRESS+0x10)
#define SGMIITX1_OBSV0				(SGMII_PHY_DEBUG_ADDRESS+0x14)

#define SGMIIRX0_CTR0				(SGMII_PHY_DEBUG_ADDRESS+0x18)
#define SGMIIRX0_CTR1				(SGMII_PHY_DEBUG_ADDRESS+0x1c)
#define SGMIIRX0_OBSV0				(SGMII_PHY_DEBUG_ADDRESS+0x20)
#define SGMIIRX0_OBSV1				(SGMII_PHY_DEBUG_ADDRESS+0x24)
#define SGMIIRX1_CTR0				(SGMII_PHY_DEBUG_ADDRESS+0x28)
#define SGMIIRX1_CTR1				(SGMII_PHY_DEBUG_ADDRESS+0x2c)
#define SGMIIRX1_OBSV0				(SGMII_PHY_DEBUG_ADDRESS+0x30)
#define SGMIIRX1_OBSV1				(SGMII_PHY_DEBUG_ADDRESS+0x34)

#define SGMIIAFERX0_CTR0			(SGMII_PHY_DEBUG_ADDRESS+0x38)
#define SGMIIAFERX0_CTR1			(SGMII_PHY_DEBUG_ADDRESS+0x3c)
#define SGMIIAFERX0_CTR2			(SGMII_PHY_DEBUG_ADDRESS+0x40)
#define SGMIIAFERX0_CTR3			(SGMII_PHY_DEBUG_ADDRESS+0x44)
#define SGMIIAFERX0_CTR4			(SGMII_PHY_DEBUG_ADDRESS+0x48)
#define SGMIIAFERX0_CTR5			(SGMII_PHY_DEBUG_ADDRESS+0x4c)
#define SGMIIAFERX0_CTR6			(SGMII_PHY_DEBUG_ADDRESS+0x50)
#define SGMIIAFERX0_CTR7			(SGMII_PHY_DEBUG_ADDRESS+0x54)
#define SGMIIAFERX0_CTR8			(SGMII_PHY_DEBUG_ADDRESS+0x58)
#define SGMIIAFERX0_CTR9			(SGMII_PHY_DEBUG_ADDRESS+0x5c)
#define SGMIIAFERX0_CTR10			(SGMII_PHY_DEBUG_ADDRESS+0x60)
#define SGMIIAFERX0_CTR11			(SGMII_PHY_DEBUG_ADDRESS+0x64)
#define SGMIIAFERX0_CTR12			(SGMII_PHY_DEBUG_ADDRESS+0x68)
#define SGMIIAFERX0_CTR13			(SGMII_PHY_DEBUG_ADDRESS+0x6c)
#define SGMIIAFERX0_CTR14			(SGMII_PHY_DEBUG_ADDRESS+0x70)
#define SGMIIAFERX0_OBSV0			(SGMII_PHY_DEBUG_ADDRESS+0x74)
#define SGMIIAFERX0_OBSV1			(SGMII_PHY_DEBUG_ADDRESS+0x78)

#define SGMIIAFERX1_CTR0			(SGMII_PHY_DEBUG_ADDRESS+0x7c)
#define SGMIIAFERX1_CTR1			(SGMII_PHY_DEBUG_ADDRESS+0x80)
#define SGMIIAFERX1_CTR2			(SGMII_PHY_DEBUG_ADDRESS+0x84)
#define SGMIIAFERX1_CTR3			(SGMII_PHY_DEBUG_ADDRESS+0x88)
#define SGMIIAFERX1_CTR4			(SGMII_PHY_DEBUG_ADDRESS+0x8c)
#define SGMIIAFERX1_CTR5			(SGMII_PHY_DEBUG_ADDRESS+0x90)
#define SGMIIAFERX1_CTR6			(SGMII_PHY_DEBUG_ADDRESS+0x94)
#define SGMIIAFERX1_CTR7			(SGMII_PHY_DEBUG_ADDRESS+0x98)
#define SGMIIAFERX1_CTR8			(SGMII_PHY_DEBUG_ADDRESS+0x9c)
#define SGMIIAFERX1_CTR9			(SGMII_PHY_DEBUG_ADDRESS+0xa0)
#define SGMIIAFERX1_CTR10			(SGMII_PHY_DEBUG_ADDRESS+0xa4)
#define SGMIIAFERX1_CTR11			(SGMII_PHY_DEBUG_ADDRESS+0xa8)
#define SGMIIAFERX1_CTR12			(SGMII_PHY_DEBUG_ADDRESS+0xac)
#define SGMIIAFERX1_CTR13			(SGMII_PHY_DEBUG_ADDRESS+0xb0)
#define SGMIIAFERX1_CTR14			(SGMII_PHY_DEBUG_ADDRESS+0xb4)
#define SGMIIAFERX1_OBSV0			(SGMII_PHY_DEBUG_ADDRESS+0xb8)
#define SGMIIAFERX1_OBSV1			(SGMII_PHY_DEBUG_ADDRESS+0xbc)
#define SGMIIPLL_CTR1				(SGMII_PHY_DEBUG_ADDRESS+0xc0)
#define SGMIIPLL_CTR2				(SGMII_PHY_DEBUG_ADDRESS+0xc4)
#define SGMIIPLL_CTR3				(SGMII_PHY_DEBUG_ADDRESS+0xc8)
#define SGMIIPLL_CTR4				(SGMII_PHY_DEBUG_ADDRESS+0xcc)
#define SGMIIPLL_OBSV0				(SGMII_PHY_DEBUG_ADDRESS+0xd0)

static u32 sgmii_readl(u32 offset)
{
	u32 val = 0;

	regmap_read(G_SGMII_REGMAP, offset, &val);
	return val;
}

static void sgmii_writel(u32 offset, u32 val)
{
	regmap_write(G_SGMII_REGMAP, offset, val);
}

static void sgmii_serdes_ch0_rxobsv_enable(void)
{
	u32 rxctl0;

	rxctl0 = sgmii_readl(SGMIIAFERX0_CTR5);
	rxctl0 &= ~0x3F00;
	rxctl0 |= (3 << 8);
	sgmii_writel(SGMIIAFERX0_CTR5, rxctl0);
}

static void sgmii_serdes_ch1_rxobsv_enable(void)
{
	u32 rxctl0;

	rxctl0 = sgmii_readl(SGMIIAFERX1_CTR5);
	rxctl0 &= ~0x3F00;
	rxctl0 |= (3 << 8);
	sgmii_writel(SGMIIAFERX1_CTR5, rxctl0);
}

static void sgmii_serdes_1000_pll(void)
{
        u32 ctrl1, pll_ctrl1, pll_ctrl2, pll_ctrl3, pll_ctrl4;

	pll_ctrl1 = 0x18500010;
	pll_ctrl2 = 0x320a4200;
	pll_ctrl3 = 0x000c4002;
	pll_ctrl4 = 0x00000000;

	sgmii_writel(SGMIIPLL_CTR1, pll_ctrl1);
	sgmii_writel(SGMIIPLL_CTR2, pll_ctrl2);
	sgmii_writel(SGMIIPLL_CTR3, pll_ctrl3);
	sgmii_writel(SGMIIPLL_CTR4, pll_ctrl4);

	// reset PLL
	ctrl1 = sgmii_readl(SGMIIPLL_CTR1);
	sgmii_writel(SGMIIPLL_CTR1, ctrl1 & (~PLL_RST_MASK));
	mdelay(1);
	sgmii_writel(SGMIIPLL_CTR1, ctrl1);
	mdelay(1);
}

static void sgmii_serdes_1000_ch0_init(int data_rate)
{
	//==========
	//Config TX
	//==========
	sgmii_writel(SGMIITX0_CTR0,0x0163004d);      //sgmii_tx_enable  0x0 -> 0x1               :power enable
	sgmii_writel(SGMIITX0_CTR0,0x0163004d);      //sgmii_tx_lvds_rsel  0x6 -> 0x6            :tun Res
	sgmii_writel(SGMIITX0_CTR0,0x0163004d);      //sgmii_tx_bias_en  0x1 -> 0x1              :power on
	sgmii_writel(SGMIITX0_CTR0,0x0164004d);      //sgmii_tx_pib  0x3 -> 0x4                  :power on
	sgmii_writel(SGMIITX0_CTR0,0x0164104d);      //sgmii_tx_vcmset  0x0 -> 0x1               :power on
	sgmii_writel(SGMIITX0_CTR1,0x00000000);      //sgmii_clkmode_sel  0x0 -> 0x0             :send clk
	//==========
	//Special config for bringup, please comment it in simulation
	//==========
	sgmii_writel(SGMIITX0_CTR0,0x0164104d);         //sgmii_tx_clksel_bypass  0x0 -> 0x0        :use clkmux
	sgmii_writel(SGMIITX0_CTR0,0x0164104d);         //sgmii_tx_clksel  0x0 -> 0x0               :1
	sgmii_writel(SGMIIAFERX0_CTR2,0x021131a8);      //rct_manual_lckdet  0x0 -> 0x1             :manual lpbk mode rx
	sgmii_writel(SGMIIAFERX0_CTR2,0x023131a8);      //rct_en_lckdet  0x0 -> 0x1                 :manual lpbk mode rx
	//==========
	//Config RX
	//==========
        sgmii_writel(SGMIIAFERX0_CTR0,0x08001429);      //bypasspi  0x1 -> 0x0                        //enable pi
	sgmii_writel(SGMIIAFERX0_CTR5,0xb0714033);      //ictrl_ckgen  0x3 -> 0x1                :reduce dco current
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095e05);      //en_dither  0x0 -> 0x1                  :enable dither
        sgmii_writel(SGMIIAFERX0_CTR0,0x08001429);      //en_dsm  0x1 -> 0x1                          //disable cdr dsm
	sgmii_writel(SGMIIAFERX0_CTR3,0x00070585);      //datarate_sel  0x1 -> 0x1               :3
	sgmii_writel(SGMIIAFERX0_CTR7,0x01afb356);      //freq_adjust  0x0 -> 0x1afb             :3125M freq adjust for FA, (4608/3125-1)*8192
	sgmii_writel(SGMIIAFERX0_CTR5,0xb0a14033);      //phased_fr_gain  0x7 -> 0xa             :phased_fr_gain = a
	sgmii_writel(SGMIIAFERX0_CTR7,0x01afb354);      //div_int_dco  0x6 -> 0x4                :div_int = 4
	sgmii_writel(SGMIIAFERX0_CTR7,0x01afb3f4);      //div_p_dco  0x5 -> 0xf                  :div_p = f
        sgmii_writel(SGMIIAFERX0_CTR0,0x08001c29);      //slvs_mode  0x0 -> 0x1                       //datarate4Gbps, slvs_mode= 1
	sgmii_writel(SGMIIAFERX0_CTR4,0xd253bc00);      //bypassVgaVcmCalib  0x0 -> 0x1          :bypass VGA VCM in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xd253bc00);      //bypassCtleVcmCalib  0x1 -> 0x1         :bypass CTLE VCM in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xd253be00);      //bypassSlicerBufVcmCalib  0x0 -> 0x1    :bypass SlicerBuf VCM in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xd253bf00);      //bypassSummerVcmCalib  0x0 -> 0x1       :bypass DFE summer VCM in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR5,0xb0a94033);      //bypassVgaOffsetCalib  0x0 -> 0x1       :bypass VGA offset in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xda53bf00);      //bypassCtle1OffsetCalib  0x0 -> 0x1     :bypass CTLE1 offset in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xda53bf00);      //bypassCtle2OffsetCalib  0x1 -> 0x1     :bypass CTLE2 offset in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xda5bbf00);      //bypassSlicerOffsetCalib  0x0 -> 0x1    :bypass Slicer offset in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR2,0x023121a8);      //pd_calib  0x1 -> 0x0                   :bug work around, don't power down calib to allow vcm comparator work during sq detection
	sgmii_writel(SGMIIAFERX0_CTR1,0x05094e05);      //voscalib_en_in  0x1 -> 0x0             :voscalib_en_in=0
	sgmii_writel(SGMIIAFERX0_CTR2,0x063121a8);      //force_phase_lock  0x0 -> 0x1           :
	sgmii_writel(SGMIIAFERX0_CTR2,0x063125a8);      //halt_dco  0x0 -> 0x1                   :halt_dco
	sgmii_writel(SGMIIAFERX0_CTR1,0x01094e05);      //ref_clk_sel_dco  0x1 -> 0x0            :ref_clk_sel_dco_rx[5] set to 1'h1, FA refclk select: =0 24MHz;=1 72MHz
        mdelay(1);
	sgmii_writel(SGMIIAFERX0_CTR2,0x063121a8);      //halt_dco  0x1 -> 0x0                   :halt_dco_rx[30] set to 1'h0 release DCO
	// Reset PHY
	sgmii_writel(SGMIITX0_CTR0,0x0164104d);         //sgmii_tx_rstn  0x0 -> 0x0              :reset tx afe
	sgmii_writel(SGMIIRX0_CTR0,0x00000101);         //sgmii_rx_rstn  0x0 -> 0x0              :reset rx afe
	//Release Reset
	sgmii_writel(SGMIITX0_CTR0,0x0164104f);         //sgmii_tx_rstn  0x0 -> 0x1              :release tx afe
	sgmii_writel(SGMIIRX0_CTR0,0x00000103);         //sgmii_rx_rstn  0x0 -> 0x1              :release rx afe
        mdelay(1);
	sgmii_writel(SGMIIAFERX0_CTR1,0x00094e05);      //pause_lf_dco  0x1 -> 0x0               :pause_lf_dco_rx[17] set to 1'h0

	// enable rxobsv for debug purpose
	sgmii_serdes_ch0_rxobsv_enable();
}

static void sgmii_serdes_1000_ch1_init(int data_rate)
{
	//==========
	//Config TX
	//==========
	sgmii_writel(SGMIITX1_CTR0,0x0163004d);      //sgmii_tx_enable  0x0 -> 0x1              :power enable
	sgmii_writel(SGMIITX1_CTR0,0x0163004d);      //sgmii_tx_lvds_rsel  0x6 -> 0x6           :tun Res
	sgmii_writel(SGMIITX1_CTR0,0x0163004d);      //sgmii_tx_bias_en  0x1 -> 0x1             :power on
	sgmii_writel(SGMIITX1_CTR0,0x0164004d);      //sgmii_tx_pib  0x3 -> 0x4                 :power on
	sgmii_writel(SGMIITX1_CTR0,0x0164104d);      //sgmii_tx_vcmset  0x0 -> 0x1              :power on
	sgmii_writel(SGMIITX1_CTR1,0x00000000);      //sgmii_clkmode_sel  0x0 -> 0x0            :send clk
	//==========
	//Special config for bringup, please comment it in simulation
	//==========
	sgmii_writel(SGMIITX1_CTR0,0x0164104d);      //sgmii_tx_clksel_bypass  0x0 -> 0x0       :use clkmux
	sgmii_writel(SGMIITX1_CTR0,0x0164104d);      //sgmii_tx_clksel  0x0 -> 0x0              :1
	sgmii_writel(SGMIIAFERX1_CTR2,0x021131a8);   //rct_manual_lckdet  0x0 -> 0x1            :manual lpbk mode rx
	sgmii_writel(SGMIIAFERX1_CTR2,0x023131a8);   //rct_en_lckdet  0x0 -> 0x1                :manual lpbk mode rx
	//==========
	//Config RX
	//==========
        sgmii_writel(SGMIIAFERX1_CTR0,0x08001429);      //bypasspi  0x1 -> 0x0                        //enable pi
	sgmii_writel(SGMIIAFERX1_CTR5,0xb0714033);      //ictrl_ckgen  0x3 -> 0x1                     :reduce dco current
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095e05);      //en_dither  0x0 -> 0x1                       :enable dither
        sgmii_writel(SGMIIAFERX1_CTR0,0x08001429);      //en_dsm  0x1 -> 0x1                          //disable cdr dsm
	sgmii_writel(SGMIIAFERX1_CTR3,0x00070585);      //datarate_sel  0x1 -> 0x1                    :3
	sgmii_writel(SGMIIAFERX1_CTR7,0x01afb356);      //freq_adjust  0x0 -> 0x1afb                  :3125M freq adjust for FA, (4608/3125-1)*8192
	sgmii_writel(SGMIIAFERX1_CTR5,0xb0a14033);      //phased_fr_gain  0x7 -> 0xa                  :phased_fr_gain = a
	sgmii_writel(SGMIIAFERX1_CTR7,0x01afb354);      //div_int_dco  0x6 -> 0x4                     :div_int = 4
	sgmii_writel(SGMIIAFERX1_CTR7,0x01afb3f4);      //div_p_dco  0x5 -> 0xf                       :div_p = f
        sgmii_writel(SGMIIAFERX1_CTR0,0x08001c29);      //slvs_mode  0x0 -> 0x1                       //datarate4Gbps, slvs_mode= 1
	sgmii_writel(SGMIIAFERX1_CTR4,0xd253bc00);      //bypassVgaVcmCalib  0x0 -> 0x1               :bypass VGA VCM in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xd253bc00);      //bypassCtleVcmCalib  0x1 -> 0x1              :bypass CTLE VCM in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xd253be00);      //bypassSlicerBufVcmCalib  0x0 -> 0x1         :bypass SlicerBuf VCM in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xd253bf00);      //bypassSummerVcmCalib  0x0 -> 0x1            :bypass DFE summer VCM in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR5,0xb0a94033);      //bypassVgaOffsetCalib  0x0 -> 0x1            :bypass VGA offset in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xda53bf00);      //bypassCtle1OffsetCalib  0x0 -> 0x1          :bypass CTLE1 offset in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xda53bf00);      //bypassCtle2OffsetCalib  0x1 -> 0x1          :bypass CTLE2 offset in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xda5bbf00);      //bypassSlicerOffsetCalib  0x0 -> 0x1         :bypass Slicer offset in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR2,0x023121a8);      //pd_calib  0x1 -> 0x0                        :bug work around, don't power down calib to allow vcm comparator work during sq detection
	sgmii_writel(SGMIIAFERX1_CTR1,0x05094e05);      //voscalib_en_in  0x1 -> 0x0                  :voscalib_en_in=0
	sgmii_writel(SGMIIAFERX1_CTR2,0x063121a8);      //force_phase_lock  0x0 -> 0x1                :
	sgmii_writel(SGMIIAFERX1_CTR2,0x063125a8);      //halt_dco  0x0 -> 0x1                        :halt_dco
	sgmii_writel(SGMIIAFERX1_CTR1,0x01094e05);      //ref_clk_sel_dco  0x1 -> 0x0                 :ref_clk_sel_dco_rx[5] set to 1'h1, FA refclk select: =0 24MHz;=1 72MHz
        mdelay(1);
	sgmii_writel(SGMIIAFERX1_CTR2,0x063121a8);      //halt_dco  0x1 -> 0x0                        :halt_dco_rx[30] set to 1'h0 release DCO
	// Reset PHY
	sgmii_writel(SGMIITX1_CTR0,0x0164104d);         //sgmii_tx_rstn  0x0 -> 0x0                   :reset tx afe
	sgmii_writel(SGMIIRX1_CTR0,0x00000101);         //sgmii_rx_rstn  0x0 -> 0x0                   :reset rx afe
	//Release Reset
	sgmii_writel(SGMIITX1_CTR0,0x0164104f);         //sgmii_tx_rstn  0x0 -> 0x1                    :release tx afe
	sgmii_writel(SGMIIRX1_CTR0,0x00000103);         //sgmii_rx_rstn  0x0 -> 0x1                    :release rx afe
        mdelay(1);
	sgmii_writel(SGMIIAFERX1_CTR1,0x00094e05);      //pause_lf_dco  0x1 -> 0x0                     :pause_lf_dco_rx[17] set to 1'h0

	// enable rxobsv for debug purpose
	sgmii_serdes_ch1_rxobsv_enable();
}

void sgmii_phy_1000_init(struct amba_bsp_priv *bsp_priv)
{
	G_SGMII_REGMAP = bsp_priv->reg_sgmii;

	if (bsp_priv->id == 0) {
		sgmii_serdes_1000_pll();
		sgmii_serdes_1000_ch0_init(1000);
	}
	if (bsp_priv->id == 1) {
		msleep(200);
		sgmii_serdes_1000_ch1_init(1000);
	}
}

/*****************************************************************************************/
static void sgmii_serdes_2500_pll(void)
{
	u32 ctrl1, pll_ctrl1, pll_ctrl2, pll_ctrl3, pll_ctrl4;

	pll_ctrl1 = 0x18500010;
	pll_ctrl2 = 0x320a4200;
	pll_ctrl3 = 0x000c4002;
	pll_ctrl4 = 0x00000000;

	// program PLL
	sgmii_writel(SGMIIPLL_CTR1, pll_ctrl1);
	sgmii_writel(SGMIIPLL_CTR2, pll_ctrl2);
	sgmii_writel(SGMIIPLL_CTR3, pll_ctrl3);
	sgmii_writel(SGMIIPLL_CTR4, pll_ctrl4);

	pll_ctrl1 = 0x18500011;
	sgmii_writel(SGMIIPLL_CTR1, pll_ctrl1);
	pll_ctrl1 = 0x18500010;
	sgmii_writel(SGMIIPLL_CTR1, pll_ctrl1);

	// reset PLL
	ctrl1 = sgmii_readl(SGMIIPLL_CTR1);
	sgmii_writel(SGMIIPLL_CTR1, ctrl1 & (~PLL_RST_MASK));
	mdelay(1);
	sgmii_writel(SGMIIPLL_CTR1, ctrl1);
	mdelay(1);
}

static void sgmii_serdes_2500_ch0_init(int data_rate)
{
	//==========
	//Config TX
	//==========
	sgmii_writel(SGMIITX0_CTR0,0x0163004d);      //sgmii_tx_enable  0x0 -> 0x1                 :power enable
	sgmii_writel(SGMIITX0_CTR0,0x0163004d);      //sgmii_tx_lvds_rsel  0x6 -> 0x6              :tun Res
	sgmii_writel(SGMIITX0_CTR0,0x0163004d);      //sgmii_tx_bias_en  0x1 -> 0x1                :power on
	sgmii_writel(SGMIITX0_CTR0,0x0164004d);      //sgmii_tx_pib  0x3 -> 0x4                    :power on
	sgmii_writel(SGMIITX0_CTR0,0x0164104d);      //sgmii_tx_vcmset  0x0 -> 0x1                 :power on
	sgmii_writel(SGMIITX0_CTR1,0x00000000);      //sgmii_clkmode_sel  0x0 -> 0x0               :send clk
	//==========
	//Special config for bringup, please comment it in simulation
	//==========
	sgmii_writel(SGMIITX0_CTR0,0x0164104d);      //sgmii_tx_clksel_bypass  0x0 -> 0x0          :use clkmux
	sgmii_writel(SGMIITX0_CTR0,0x2164104d);      //sgmii_tx_clksel  0x0 -> 0x1                 :3
	//==========
	//Config PRBS (Simulaiton Only), enable auto mode
	//==========
	sgmii_writel(SGMIIRX0_CTR0,0x00000101);      //sgmii_rx_refclk_freq  0x0 -> 0x0            :24M
	sgmii_writel(SGMIIAFERX0_CTR2,0x021131a8);   //rct_manual_lckdet  0x0 -> 0x1               :manual lpbk mode rx
	sgmii_writel(SGMIIAFERX0_CTR2,0x023131a8);   //rct_en_lckdet  0x0 -> 0x1                   :manual lpbk mode rx
	//==========
	//Config RX
	//==========
	sgmii_writel(SGMIIAFERX0_CTR4,0xd233b400);    //pibw  0x5 -> 0x3                            :increase pi bw
	sgmii_writel(SGMIIAFERX0_CTR5,0xb0734033);    //ictrl_ckgen  0x3 -> 0x3                     :dco current
	sgmii_writel(SGMIIAFERX0_CTR0,0xa8001429);    //dco_gain  0x0 -> 0x2                        : kdco = 2
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095e01);    //bypassDcc  0x1 -> 0x1                       :bypassDcc = 1
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095e01);    //bypassctle  0x1 -> 0x1                      :bypassctle=1, it's a bug here, =1 means not bypass CTLE
	sgmii_writel(SGMIIAFERX0_CTR4,0xd233b400);    //bypassVgaVcmCalib  0x0 -> 0x0               :not bypass VGA VCM in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xd233b000);    //bypassCtleVcmCalib  0x1 -> 0x0              :not bypass CTLE VCM in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xd233b000);    //bypassSlicerBufVcmCalib  0x0 -> 0x0         :not bypass SlicerBuf VCM in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xd233b000);    //bypassSummerVcmCalib  0x0 -> 0x0            :not bypass DFE summer VCM in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR5,0xb07b4033);    //bypassVgaOffsetCalib  0x0 -> 0x1            :bypass VGA offset in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xda33b000);    //bypassCtle1OffsetCalib  0x0 -> 0x1          :bypass CTLE1 offset in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xda33b000);    //bypassCtle2OffsetCalib  0x1 -> 0x1          :bypass CTLE2 offset in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xda3bb000);    //bypassSlicerOffsetCalib  0x0 -> 0x1         :bypass Slicer offset in calibration controller
	sgmii_writel(SGMIIAFERX0_CTR4,0xda3bb0fe);    //calibLckDetWin  0x0 -> 0xfe                 :calibration lock check duration, set to 80*13
	sgmii_writel(SGMIIAFERX0_CTR3,0x00070585);    //calibLckDetThrsh  0x0 -> 0x0                :calibration lock check threash hold, set to 0
	sgmii_writel(SGMIIAFERX0_CTR11,0x8800f700);   //ctle_rctrl0_in  0x0 -> 0x7                  :set ctle_rctrl0_in 7
	sgmii_writel(SGMIIAFERX0_CTR11,0x88007700);   //ctle_rctrl1_in  0xf -> 0x7                  :set ctle_rctrl1_in 7
	sgmii_writel(SGMIIAFERX0_CTR11,0x88007700);   //ctle_cctrl0_in  0x0 -> 0x0                  :set ctle_cctrl0_in=0
	sgmii_writel(SGMIIAFERX0_CTR11,0x88007700);   //ctle_cctrl1_in  0x0 -> 0x0                  :set ctle_cctrl1_in=0
	sgmii_writel(SGMIIAFERX0_CTR3,0x00070685);    //datarate_sel  0x1 -> 0x2                    :3
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095e01);    //dcc_manual  0x1 -> 0x1                      :dcc_manual = 1
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095e01);    //dcc_swap  0x1 -> 0x1                        :dcc_swap = 1
	sgmii_writel(SGMIIAFERX0_CTR7,0x00000354);    //div_int_dco  0x6 -> 0x4                     :div_int = 4
	sgmii_writel(SGMIIAFERX0_CTR7,0x000003f4);    //div_p_dco  0x5 -> 0xf                       :div_p = f
	sgmii_writel(SGMIIAFERX0_CTR0,0xa8001429);    //en_dsm  0x1 -> 0x1
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095e05);    //en_dither  0x0 -> 0x1                       :enable dither
	sgmii_writel(SGMIIAFERX0_CTR7,0x00f303f4);    //freq_adjust  0x0 -> 0xf30                   :3125M freq adjust for FA, (4608/3125-1)*8192
	sgmii_writel(SGMIIAFERX0_CTR2,0x023131a8);    //gainSel_slicerBuf_in  0x0 -> 0x0            :set gainSel_slicerBuf_in
	sgmii_writel(SGMIIAFERX0_CTR2,0x023131a8);    //gainSel_unitSummer_in  0x0 -> 0x0           :set gainSel_unitSummer_in
	sgmii_writel(SGMIIAFERX0_CTR12,0x0000000f);   //lpgain_vcm  0x0 -> 0xf                      :lpgain of calibration, set to f
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095e05);    //manual_vgaCtle_offset  0x0 -> 0x0           :manual_vgaCtle_offset = 0
	sgmii_writel(SGMIIAFERX0_CTR0,0xa8001429);    //manual_eq_vcm  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX0_CTR0,0xa8001429);    //manual_ctle_vcm  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX0_CTR0,0xa8001429);    //manual_dfe_vcm  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX0_CTR0,0xa8001429);    //manual_slicer_buffer_vcm  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX0_CTR10,0x00000000);   //offset_code_in  0x0 -> 0x0                  :offset_code_in = 6'h0
	sgmii_writel(SGMIIAFERX0_CTR2,0x027131a8);    //pd_sf  0x0 -> 0x1                           :pd_sf = 1 Power Down Source Follower to Decouple Tester Offset Bug
	sgmii_writel(SGMIIAFERX0_CTR2,0x027121a8);    //pd_calib  0x1 -> 0x0                        :power on calibration circuit
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095605);    //pdeye  0x1 -> 0x0                           :enable eye momnitor path for calibration
	sgmii_writel(SGMIIAFERX0_CTR1,0x05095205);    //pderr  0x1 -> 0x0                           :enable err slicer
	sgmii_writel(SGMIIAFERX0_CTR5,0xb0ab4033);    //phased_fr_gain  0x7 -> 0xa                  :phased_fr_gain = a
	sgmii_writel(SGMIIAFERX0_CTR1,0x01095205);    //ref_clk_sel_dco  0x1 -> 0x0                 :ref_clk_sel_dco_rx[5] set to 1'h1, FA refclk select: =0 24MHz;=1 72MHz
	sgmii_writel(SGMIIAFERX0_CTR2,0x027121a8);    //sf_ictrl  0x0 -> 0x0                        :sf_ictrl = 0
	sgmii_writel(SGMIIAFERX0_CTR2,0xc27121a8);    //slicerBW  0x0 -> 0x3                        :set slicer BW to be the fastest
	sgmii_writel(SGMIIAFERX0_CTR3,0x03070685);    //slicerBufBW  0x0 -> 0x3                     :set slicer_buf BW to be the fastest
	sgmii_writel(SGMIIAFERX0_CTR0,0xa8009429);    //slicerSelIQ  0x0 -> 0x1                     :choose IQ slicer mode = 0
	sgmii_writel(SGMIIAFERX0_CTR1,0x01095205);    //slicer_offset_manual  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX0_CTR0,0xa8009c29);    //slvs_mode  0x0 -> 0x1                       :datarate4Gbps, slvs_mode= 1, dco freq=datarate
	sgmii_writel(SGMIIAFERX0_CTR1,0x010d5205);    //sgnvref  0x0 -> 0x1                         :set sgnvref to be 1'b1
	sgmii_writel(SGMIIAFERX0_CTR12,0x1000000f);   //sslms_vga_rgain  0x0 -> 0x1                 :config vga rgain
	sgmii_writel(SGMIIAFERX0_CTR2,0xc27121a8);    //sslms_vga_rpause  0x1 -> 0x1                :enable vga rpause
	sgmii_writel(SGMIIAFERX0_CTR2,0xc27121a8);    //sslms_vga_manual  0x0 -> 0x0                :sslms_vga_manual = 0
	sgmii_writel(SGMIIAFERX0_CTR2,0xc2712188);    //sslms_vref_pause  0x1 -> 0x0                :disable vref_pause
	sgmii_writel(SGMIIAFERX0_CTR10,0x02000000);   //sslms_vref_gain  0x0 -> 0x2                 :set vref_gain
	sgmii_writel(SGMIIAFERX0_CTR2,0xc2712188);    //sslms_ctle_rpause  0x1 -> 0x1               :enable ctle rpause
	sgmii_writel(SGMIIAFERX0_CTR2,0xc2712188);    //sslms_ctle_cpause  0x1 -> 0x1               :enable ctle cpause
	sgmii_writel(SGMIIAFERX0_CTR2,0xc2712188);    //sslms_ctle_manual  0x0 -> 0x0               :set ctle_maunal_mode= 0
	sgmii_writel(SGMIIAFERX0_CTR2,0xc2712180);    //sslms_dfe_pause  0x1 -> 0x0                 :disable dfe pause
	sgmii_writel(SGMIIAFERX0_CTR8,0x00000000);    //sslms_dfe_gain  0x0 -> 0x0                  :set dfe_rgain
	sgmii_writel(SGMIIAFERX0_CTR10,0x0200001f);   //vcm_set_in  0x0 -> 0x1f                     :set reference voltage = 1f for auto calibration controller
	sgmii_writel(SGMIIAFERX0_CTR11,0x88007700);   //vcm_vga_ctrl_in  0x8 -> 0x8                 :set VGA Ctrl in = 8
	sgmii_writel(SGMIIAFERX0_CTR11,0x88007700);   //vcm_ctle_ctrlpin_in  0x8 -> 0x8             :set default CTLE vcm
	sgmii_writel(SGMIIAFERX0_CTR11,0x88017700);   //vga_rctrl_in  0x0 -> 0x1                    :set vga_rctrl_in= 1
	sgmii_writel(SGMIIAFERX0_CTR5,0x80ab4033);    //vocmdfebuf_in  0x16 -> 0x10                 :set default slicerBuffer vcm
	sgmii_writel(SGMIIAFERX0_CTR5,0x80ab4030);    //vocmdfe_in  0x13 -> 0x10                    :set default unitSummer vcm
	sgmii_writel(SGMIIAFERX0_CTR1,0x010d5205);    //voscalib_en_in  0x1 -> 0x1                  :voscalib_en=1
	sgmii_writel(SGMIIAFERX0_CTR9,0x20001060);    //vref_in  0x40 -> 0x60                       :set vref_in = 60
	sgmii_writel(SGMIIAFERX0_CTR2,0xc6712180);    //force_phase_lock  0x0 -> 0x1
	sgmii_writel(SGMIIAFERX0_CTR3,0x03040685);    //ctle_func_en_in  0x3 -> 0x0                 :ctle_func_en_in = 0
	sgmii_writel(SGMIIAFERX0_CTR3,0x03000685);    //vga_func_en_in  0x1 -> 0x0                  :vga_func_en_in = 0
	//==============================
	//=== sequential programming ===
	//==============================
	sgmii_writel(SGMIIAFERX0_CTR2,0xc6712580);      //halt_dco  0x0 -> 0x1                      :halt_dco
	mdelay(1);
	sgmii_writel(SGMIIAFERX0_CTR2,0xc6712180);      //halt_dco  0x1 -> 0x0                      :halt_dco_rx[30] set to 1'h0 release DCO
	// Reset PHY
	sgmii_writel(SGMIITX0_CTR0,0x2164104d);         //sgmii_tx_rstn  0x0 -> 0x0                 :reset tx afe
	sgmii_writel(SGMIIRX0_CTR0,0x00000101);         //sgmii_rx_rstn  0x0 -> 0x0                 :reset rx afe
	//Release Reset
	sgmii_writel(SGMIIRX0_CTR0,0x00000103);         //sgmii_rx_rstn  0x0 -> 0x1                 :release rx afe
	//====================
	//=== offset calib ===
	//====================
	sgmii_writel(SGMIIAFERX0_CTR5,0x84ab4030);      //sel_calib  0x0 -> 0x4                     :select vga offset calibration
	sgmii_writel(SGMIIAFERX0_CTR12,0x10000009);     //lpgain_vcm  0xf -> 0x9                    :lpgain of calibration, set to 9
	sgmii_writel(SGMIIAFERX0_CTR1,0x210d5205);      //start_calib  0x0 -> 0x1                   :start calibration
	mdelay(1);
	sgmii_writel(SGMIIAFERX0_CTR5,0x85ab4030);      //sel_calib  0x4 -> 0x5                     :select ctle1 offset calibration
	mdelay(1);
	sgmii_writel(SGMIIAFERX0_CTR5,0x86ab4030);      //sel_calib  0x5 -> 0x6                     :select ctle2 offset calibration
	mdelay(1);
	sgmii_writel(SGMIIAFERX0_CTR1,0x010d5205);      //start_calib  0x1 -> 0x0                   :end calibration
	//=========================================
	//Calibration done,re-config for loopbk
	//=========================================
	sgmii_writel(SGMIIAFERX0_CTR2,0xc6712180);      //calibdone  0x1 -> 0x1                     :calibdone = 1, disalbe dcc detecotor
	sgmii_writel(SGMIIAFERX0_CTR2,0xc6312180);      //pd_sf  0x1 -> 0x0                         :pd_sf = 0 Enable Source Follow for Loopbk test after Calibration
	sgmii_writel(SGMIIAFERX0_CTR1,0x010d5a05);      //pdeye  0x0 -> 0x1                         :disable eye momnitor path
	sgmii_writel(SGMIIAFERX0_CTR2,0xc6313180);      //pd_calib  0x0 -> 0x1                      :power down calibration circuit
	sgmii_writel(SGMIIAFERX0_CTR1,0x010d4a05);      //voscalib_en_in  0x1 -> 0x0                :voscalib_en_in=0
	//=========================================
	//Here start the TX transmitting PRBS data
	//=========================================
	sgmii_writel(SGMIITX0_CTR0,0x2164104f);         //sgmii_tx_rstn  0x0 -> 0x1                 :release tx afe
	sgmii_writel(SGMIIAFERX0_CTR1,0x000d4a05);      //pause_lf_dco  0x1 -> 0x0                  :pause_lf_dco_rx[17] set to 1'h0
        mdelay(20);
	//================================
	//EQ
	//================================
	sgmii_writel(SGMIIAFERX0_CTR3,0x03002685);      //sslms_eq_mode  0x0 -> 0x2                 :enable vref SSLMS loop
	mdelay(40);
	sgmii_writel(SGMIIAFERX0_CTR3,0x03004685);      //sslms_eq_mode  0x2 -> 0x4                 :enable dfe SSLMS loop
	mdelay(60);
	sgmii_writel(SGMIIAFERX0_CTR2,0xc6313188);      //sslms_dfe_pause  0x0 -> 0x1               :enable dfe pause
	sgmii_writel(SGMIIAFERX0_CTR2,0xc63131a8);      //sslms_vref_pause  0x0 -> 0x1              :enable vref_pause

	// enable rxobsv for debug purpose
	sgmii_serdes_ch0_rxobsv_enable();
}

static void sgmii_serdes_2500_ch1_init(int data_rate)
{
	//==========
	//Config TX
	//==========
	sgmii_writel(SGMIITX1_CTR0,0x0163004d);      //sgmii_tx_enable  0x0 -> 0x1                  :power enable
	sgmii_writel(SGMIITX1_CTR0,0x0163004d);      //sgmii_tx_lvds_rsel  0x6 -> 0x6               :tun Res
	sgmii_writel(SGMIITX1_CTR0,0x0163004d);      //sgmii_tx_bias_en  0x1 -> 0x1                 :power on
	sgmii_writel(SGMIITX1_CTR0,0x0164004d);      //sgmii_tx_pib  0x3 -> 0x4                     :power on
	sgmii_writel(SGMIITX1_CTR0,0x0164104d);      //sgmii_tx_vcmset  0x0 -> 0x1                  :power on
	sgmii_writel(SGMIITX1_CTR1,0x00000000);      //sgmii_clkmode_sel  0x0 -> 0x0                :send clk
	//==========
	//Special config for bringup, please comment it in simulation
	//==========
	sgmii_writel(SGMIITX1_CTR0,0x0164104d);        //sgmii_tx_clksel_bypass  0x0 -> 0x0         :use clkmux
	sgmii_writel(SGMIITX1_CTR0,0x2164104d);        //sgmii_tx_clksel  0x0 -> 0x1                :3
	//==========
	//Config PRBS (Simulaiton Only), enable auto mode
	//==========
	sgmii_writel(SGMIIRX1_CTR0,0x00000101);         //sgmii_rx_refclk_freq  0x0 -> 0x0           :24M
	sgmii_writel(SGMIIAFERX1_CTR2,0x021131a8);      //rct_manual_lckdet  0x0 -> 0x1              :manual lpbk mode rx
	sgmii_writel(SGMIIAFERX1_CTR2,0x023131a8);      //rct_en_lckdet  0x0 -> 0x1                  :manual lpbk mode rx
	//==========
	//Config RX
	//==========
	sgmii_writel(SGMIIAFERX1_CTR4,0xd233b400);      //pibw  0x5 -> 0x3                            :increase pi bw
	sgmii_writel(SGMIIAFERX1_CTR5,0xb0734033);      //ictrl_ckgen  0x3 -> 0x3                     :dco current
	sgmii_writel(SGMIIAFERX1_CTR0,0xa8001429);      //dco_gain  0x0 -> 0x2                        :kdco = 2
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095e01);      //bypassDcc  0x1 -> 0x1                       :bypassDcc = 1
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095e01);      //bypassctle  0x1 -> 0x1                      :bypassctle=1, it's a bug here, =1 means not bypass CTLE
	sgmii_writel(SGMIIAFERX1_CTR4,0xd233b400);      //bypassVgaVcmCalib  0x0 -> 0x0               :not bypass VGA VCM in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xd233b000);      //bypassCtleVcmCalib  0x1 -> 0x0              :not bypass CTLE VCM in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xd233b000);      //bypassSlicerBufVcmCalib  0x0 -> 0x0         :not bypass SlicerBuf VCM in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xd233b000);      //bypassSummerVcmCalib  0x0 -> 0x0            :not bypass DFE summer VCM in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR5,0xb07b4033);      //bypassVgaOffsetCalib  0x0 -> 0x1            :bypass VGA offset in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xda33b000);      //bypassCtle1OffsetCalib  0x0 -> 0x1          :bypass CTLE1 offset in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xda33b000);      //bypassCtle2OffsetCalib  0x1 -> 0x1          :bypass CTLE2 offset in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xda3bb000);      //bypassSlicerOffsetCalib  0x0 -> 0x1         :bypass Slicer offset in calibration controller
	sgmii_writel(SGMIIAFERX1_CTR4,0xda3bb0fe);      //calibLckDetWin  0x0 -> 0xfe                 :calibration lock check duration, set to 80*13
	sgmii_writel(SGMIIAFERX1_CTR3,0x00070585);      //calibLckDetThrsh  0x0 -> 0x0                :calibration lock check threash hold, set to 0
	sgmii_writel(SGMIIAFERX1_CTR11,0x8800f700);     //ctle_rctrl0_in  0x0 -> 0x7                  :set ctle_rctrl0_in 7
	sgmii_writel(SGMIIAFERX1_CTR11,0x88007700);     //ctle_rctrl1_in  0xf -> 0x7                  :set ctle_rctrl1_in 7
	sgmii_writel(SGMIIAFERX1_CTR11,0x88007700);     //ctle_cctrl0_in  0x0 -> 0x0                  :set ctle_cctrl0_in=0
	sgmii_writel(SGMIIAFERX1_CTR11,0x88007700);     //ctle_cctrl1_in  0x0 -> 0x0                  :set ctle_cctrl1_in=0
	sgmii_writel(SGMIIAFERX1_CTR3,0x00070685);      //datarate_sel  0x1 -> 0x2                    :3
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095e01);      //dcc_manual  0x1 -> 0x1                      :dcc_manual = 1
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095e01);      //dcc_swap  0x1 -> 0x1                        :dcc_swap = 1
	sgmii_writel(SGMIIAFERX1_CTR7,0x00000354);      //div_int_dco  0x6 -> 0x4                     :div_int = 4
	sgmii_writel(SGMIIAFERX1_CTR7,0x000003f4);      //div_p_dco  0x5 -> 0xf                       :div_p = f
	sgmii_writel(SGMIIAFERX1_CTR0,0xa8001429);      //en_dsm  0x1 -> 0x1
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095e05);      //en_dither  0x0 -> 0x1                       :enable dither
	sgmii_writel(SGMIIAFERX1_CTR7,0x00f303f4);      //freq_adjust  0x0 -> 0xf30                   :3125M freq adjust for FA, (4608/3125-1)*8192
	sgmii_writel(SGMIIAFERX1_CTR2,0x023131a8);      //gainSel_slicerBuf_in  0x0 -> 0x0            :set gainSel_slicerBuf_in
	sgmii_writel(SGMIIAFERX1_CTR2,0x023131a8);      //gainSel_unitSummer_in  0x0 -> 0x0           :set gainSel_unitSummer_in
	sgmii_writel(SGMIIAFERX1_CTR12,0x0000000f);     //lpgain_vcm  0x0 -> 0xf                      :lpgain of calibration, set to f
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095e05);      //manual_vgaCtle_offset  0x0 -> 0x0           :manual_vgaCtle_offset = 0
	sgmii_writel(SGMIIAFERX1_CTR0,0xa8001429);      //manual_eq_vcm  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX1_CTR0,0xa8001429);      //manual_ctle_vcm  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX1_CTR0,0xa8001429);      //manual_dfe_vcm  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX1_CTR0,0xa8001429);      //manual_slicer_buffer_vcm  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX1_CTR10,0x00000000);     //offset_code_in  0x0 -> 0x0  offset_code_in = 6'h0
	sgmii_writel(SGMIIAFERX1_CTR2,0x027131a8);      //pd_sf  0x0 -> 0x1                            :pd_sf = 1 Power Down Source Follower to Decouple Tester Offset Bug
	sgmii_writel(SGMIIAFERX1_CTR2,0x027121a8);      //pd_calib  0x1 -> 0x0                         :power on calibration circuit
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095605);      //pdeye  0x1 -> 0x0                            :enable eye momnitor path for calibration
	sgmii_writel(SGMIIAFERX1_CTR1,0x05095205);      //pderr  0x1 -> 0x0                            :enable err slicer
	sgmii_writel(SGMIIAFERX1_CTR5,0xb0ab4033);      //phased_fr_gain  0x7 -> 0xa                   :phased_fr_gain = a
	sgmii_writel(SGMIIAFERX1_CTR1,0x01095205);      //ref_clk_sel_dco  0x1 -> 0x0                  :ref_clk_sel_dco_rx[5] set to 1'h1, FA refclk select: =0 24MHz;=1 72MHz
	sgmii_writel(SGMIIAFERX1_CTR2,0x027121a8);      //sf_ictrl  0x0 -> 0x0                         :sf_ictrl = 0
	sgmii_writel(SGMIIAFERX1_CTR2,0xc27121a8);      //slicerBW  0x0 -> 0x3                         :set slicer BW to be the fastest
	sgmii_writel(SGMIIAFERX1_CTR3,0x03070685);      //slicerBufBW  0x0 -> 0x3                      :set slicer_buf BW to be the fastest
	sgmii_writel(SGMIIAFERX1_CTR0,0xa8009429);      //slicerSelIQ  0x0 -> 0x1                      :choose IQ slicer mode = 0
	sgmii_writel(SGMIIAFERX1_CTR1,0x01095205);      //slicer_offset_manual  0x0 -> 0x0
	sgmii_writel(SGMIIAFERX1_CTR0,0xa8009c29);      //slvs_mode  0x0 -> 0x1                        :datarate4Gbps, slvs_mode= 1, dco freq=datarate
	sgmii_writel(SGMIIAFERX1_CTR1,0x010d5205);      //sgnvref  0x0 -> 0x1                          :set sgnvref to be 1'b1
	sgmii_writel(SGMIIAFERX1_CTR12,0x1000000f);     //sslms_vga_rgain  0x0 -> 0x1                  :config vga rgain
	sgmii_writel(SGMIIAFERX1_CTR2,0xc27121a8);      //sslms_vga_rpause  0x1 -> 0x1                 :enable vga rpause
	sgmii_writel(SGMIIAFERX1_CTR2,0xc27121a8);      //sslms_vga_manual  0x0 -> 0x0                 :sslms_vga_manual = 0
	sgmii_writel(SGMIIAFERX1_CTR2,0xc2712188);      //sslms_vref_pause  0x1 -> 0x0                 :disable vref_pause
	sgmii_writel(SGMIIAFERX1_CTR10,0x02000000);     //sslms_vref_gain  0x0 -> 0x2                  :set vref_gain
	sgmii_writel(SGMIIAFERX1_CTR2,0xc2712188);      //sslms_ctle_rpause  0x1 -> 0x1                :enable ctle rpause
	sgmii_writel(SGMIIAFERX1_CTR2,0xc2712188);      //sslms_ctle_cpause  0x1 -> 0x1                :enable ctle cpause
	sgmii_writel(SGMIIAFERX1_CTR2,0xc2712188);      //sslms_ctle_manual  0x0 -> 0x0                :set ctle_maunal_mode= 0
	sgmii_writel(SGMIIAFERX1_CTR2,0xc2712180);      //sslms_dfe_pause  0x1 -> 0x0                  :disable dfe pause
	sgmii_writel(SGMIIAFERX1_CTR8,0x00000000);      //sslms_dfe_gain  0x0 -> 0x0                   :set dfe_rgain
	sgmii_writel(SGMIIAFERX1_CTR10,0x0200001f);     //vcm_set_in  0x0 -> 0x1f                      :set reference voltage = 1f for auto calibration controller
	sgmii_writel(SGMIIAFERX1_CTR11,0x88007700);     //vcm_vga_ctrl_in  0x8 -> 0x8                  :set VGA Ctrl in = 8
	sgmii_writel(SGMIIAFERX1_CTR11,0x88007700);     //vcm_ctle_ctrlpin_in  0x8 -> 0x8              :set default CTLE vcm
	sgmii_writel(SGMIIAFERX1_CTR11,0x88017700);     //vga_rctrl_in  0x0 -> 0x1                     :set vga_rctrl_in= 1
	sgmii_writel(SGMIIAFERX1_CTR5,0x80ab4033);      //vocmdfebuf_in  0x16 -> 0x10                  :set default slicerBuffer vcm
	sgmii_writel(SGMIIAFERX1_CTR5,0x80ab4030);      //vocmdfe_in  0x13 -> 0x10                     :set default unitSummer vcm
	sgmii_writel(SGMIIAFERX1_CTR1,0x010d5205);      //voscalib_en_in  0x1 -> 0x1                   :voscalib_en=1
	sgmii_writel(SGMIIAFERX1_CTR9,0x20001060);      //vref_in  0x40 -> 0x60  set                   :vref_in = 60
	sgmii_writel(SGMIIAFERX1_CTR2,0xc6712180);      //force_phase_lock  0x0 -> 0x1
	sgmii_writel(SGMIIAFERX1_CTR3,0x03040685);      //ctle_func_en_in  0x3 -> 0x0                  :ctle_func_en_in = 0
	sgmii_writel(SGMIIAFERX1_CTR3,0x03000685);      //vga_func_en_in  0x1 -> 0x0                   :vga_func_en_in = 0
	//==============================
	//=== sequential programming ===
	//==============================
	sgmii_writel(SGMIIAFERX1_CTR2,0xc6712580);      //halt_dco  0x0 -> 0x1                         :halt_dco
        mdelay(1);
	sgmii_writel(SGMIIAFERX1_CTR2,0xc6712180);      //halt_dco  0x1 -> 0x0                         :halt_dco_rx[30] set to 1'h0 release DCO
	// Reset PHY
	sgmii_writel(SGMIITX1_CTR0,0x2164104d);         //sgmii_tx_rstn  0x0 -> 0x0                    :reset tx afe
	sgmii_writel(SGMIIRX1_CTR0,0x00000101);         //sgmii_rx_rstn  0x0 -> 0x0                    :reset rx afe
	//Release Reset
	sgmii_writel(SGMIIRX1_CTR0,0x00000103);         //sgmii_rx_rstn  0x0 -> 0x1                    :release rx afe
	//====================
	//=== offset calib ===
	//====================
	sgmii_writel(SGMIIAFERX1_CTR5,0x84ab4030);      //sel_calib  0x0 -> 0x4                        :select vga offset calibration
	sgmii_writel(SGMIIAFERX1_CTR12,0x10000009);     //lpgain_vcm  0xf -> 0x9                       :lpgain of calibration, set to 9
	sgmii_writel(SGMIIAFERX1_CTR1,0x210d5205);      //start_calib  0x0 -> 0x1                      :start calibration
        mdelay(1);
	sgmii_writel(SGMIIAFERX1_CTR5,0x85ab4030);      //sel_calib  0x4 -> 0x5                        :select ctle1 offset calibration
        mdelay(1);
	sgmii_writel(SGMIIAFERX1_CTR5,0x86ab4030);      //sel_calib  0x5 -> 0x6                        :select ctle2 offset calibration
        mdelay(1);
	sgmii_writel(SGMIIAFERX1_CTR1,0x010d5205);      //start_calib  0x1 -> 0x0                      :end calibration
	//=========================================
	//Calibration done,re-config for loopbk
	//=========================================
	sgmii_writel(SGMIIAFERX1_CTR2,0xc6712180);      //calibdone  0x1 -> 0x1                        :calibdone = 1, disalbe dcc detecotor
	sgmii_writel(SGMIIAFERX1_CTR2,0xc6312180);      //pd_sf  0x1 -> 0x0  pd_sf = 0                 :Enable Source Follow for Loopbk test after Calibration
	sgmii_writel(SGMIIAFERX1_CTR1,0x010d5a05);      //pdeye  0x0 -> 0x1                            :disable eye momnitor path
	sgmii_writel(SGMIIAFERX1_CTR2,0xc6313180);      //pd_calib  0x0 -> 0x1                         :power down calibration circuit
	sgmii_writel(SGMIIAFERX1_CTR1,0x010d4a05);      //voscalib_en_in  0x1 -> 0x0                   :voscalib_en_in=0
	//=========================================
	//Here start the TX transmitting PRBS data
	//=========================================
	sgmii_writel(SGMIITX1_CTR0,0x2164104f);         //sgmii_tx_rstn  0x0 -> 0x1                    :release tx afe
	sgmii_writel(SGMIIAFERX1_CTR1,0x000d4a05);      //pause_lf_dco  0x1 -> 0x0                     :pause_lf_dco_rx[17] set to 1'h0
        mdelay(20);
	//================================
	//EQ
	//================================
	sgmii_writel(SGMIIAFERX1_CTR3,0x03002685);      //sslms_eq_mode  0x0 -> 0x2                    :enable vref SSLMS loop
	mdelay(40);
	sgmii_writel(SGMIIAFERX1_CTR3,0x03004685);      //sslms_eq_mode  0x2 -> 0x4                    :enable dfe SSLMS loop
	mdelay(60);
	sgmii_writel(SGMIIAFERX1_CTR2,0xc6313188);      //sslms_dfe_pause  0x0 -> 0x1                  :enable dfe pause
	sgmii_writel(SGMIIAFERX1_CTR2,0xc63131a8);      //sslms_vref_pause  0x0 -> 0x1                 :enable vref_pause

	// enable rxobsv for debug purpose
	sgmii_serdes_ch1_rxobsv_enable();
}

static void sgmii_phy_2500_init(struct amba_bsp_priv *bsp_priv)
{
	G_SGMII_REGMAP = bsp_priv->reg_sgmii;

	if (bsp_priv->id == 0) {
		sgmii_serdes_2500_pll();
		sgmii_serdes_2500_ch0_init(2500);
	}

	if (bsp_priv->id == 1) {
		msleep(200);
		sgmii_serdes_2500_ch1_init(2500);
	}
}

/* serdes-rx lost lock and reset-serdes-rx
 * to let the serdes-rx lock the clock again
 * rxobsv:0x200,0x5FF is the typical bad value
*/
static void serdes_bsp_fixup(void *priv)
{
	u32 rxobsv, rxctl0;
	struct amba_bsp_priv *bsp_priv = priv;

	if (bsp_priv->id == 0) {
		rxobsv = sgmii_readl(SGMIIAFERX0_OBSV1) & 0x7FF;
		if (rxobsv == 0x200 || rxobsv == 0x5FF)
			bsp_priv->rxlost_cnt++;
		else
			bsp_priv->rxlost_cnt = 0;

		pr_info("SGMIIAFERX0_OBSV1[10:0] => 0x%x \n", rxobsv);
		if (bsp_priv->rxlost_cnt > 10) {
			bsp_priv->rxlost_cnt = 0;
			rxctl0 = sgmii_readl(SGMIIRX0_CTR0);
			sgmii_writel(SGMIIRX0_CTR0, (rxctl0 & ~BIT(1)));
			mdelay(2);
			sgmii_writel(SGMIIRX0_CTR0, (rxctl0 | BIT(1)));
		}
	}

	if (bsp_priv->id == 1) {
		rxobsv = sgmii_readl(SGMIIAFERX1_OBSV1) & 0x7FF;
		if (rxobsv == 0x200 || rxobsv == 0x5FF)
			bsp_priv->rxlost_cnt++;
		else
		 	bsp_priv->rxlost_cnt = 0;

		pr_info("SGMIIAFERX1_OBSV1[10:0] => 0x%x \n", rxobsv);
		if (bsp_priv->rxlost_cnt > 10) {
			bsp_priv->rxlost_cnt = 0;
			rxctl0 = sgmii_readl(SGMIIRX1_CTR0);
			sgmii_writel(SGMIIRX1_CTR0, (rxctl0 & ~BIT(1)));
			mdelay(2);
			sgmii_writel(SGMIIRX1_CTR0, (rxctl0 | BIT(1)));
		}
	}
}

/* review pcs link status and does a rx reset if not linkup */
static void serdes_fix_mac_speed(void *priv, unsigned int speed)
{
	u32 rxctl0, pcs_link;
	struct amba_bsp_priv *bsp_priv = priv;

	if (bsp_priv->id == 0) {
		regmap_read(bsp_priv->reg_scr, 0x1F8, &pcs_link);
		if (pcs_link & BIT(0))
			return;

		/* pcs not sync, trigger serdes rx reset */
		rxctl0 = sgmii_readl(SGMIIRX0_CTR0);
		mdelay(1);
		sgmii_writel(SGMIIRX0_CTR0, (rxctl0 & ~BIT(1)));
		mdelay(1);
		sgmii_writel(SGMIIRX0_CTR0, (rxctl0 | BIT(1)));
		mdelay(1);
	}

	if (bsp_priv->id == 1) {
		regmap_read(bsp_priv->reg_scr, 0x1FC, &pcs_link);
		if (pcs_link & BIT(0))
			return;

		rxctl0 = sgmii_readl(SGMIIRX1_CTR0);
		sgmii_writel(SGMIIRX1_CTR0, (rxctl0 & ~BIT(1)));
		mdelay(1);
		sgmii_writel(SGMIIRX1_CTR0, (rxctl0 | BIT(1)));
		mdelay(1);
	}
}
