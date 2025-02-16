static char sccsid[] = "@(#)89	1.4  src/bos/kernext/mpa/mpa_write.c, sysxmpa, bos41J, 9515A_all 4/6/95 16:52:07";
/*
 *   COMPONENT_NAME: (SYSXMPA) MP/A SDLC DEVICE DRIVER
 *
 *   FUNCTIONS: mpa_write
 *		write_block
 *		xmit_timer
 *		
 *
 *   ORIGINS: 27
 *
 *   IBM CONFIDENTIAL -- (IBM Confidential Restricted when
 *   combined with the aggregated modules for this product)
 *                    SOURCE MATERIALS
 *
 *   (C) COPYRIGHT International Business Machines Corp. 1993
 *   All Rights Reserved
 *   US Government Users Restricted Rights - Use, duplication or
 *   disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */


#include <net/spl.h>
#include <sys/adspace.h>
#include <errno.h>
#include <sys/device.h>
#include <sys/dma.h>
#include <sys/ioacc.h>
#include <sys/mbuf.h>
#include <sys/sleep.h>
#include <sys/mpadd.h>
#include <sys/sysmacros.h>
#include <sys/uio.h>
#include <sys/xmem.h>
#include <sys/trchkid.h>
#include <sys/ddtrace.h>
#include <sys/except.h>
#include <sys/errids.h>

extern void recv_timer();

/*様様様様様様様様様様様様�  M P A_ W R I T E  様様様様様様様様様様様様様�
�                                                                        � 
�  NAME: mpa_write                                                       �
�                                                                        �
�  FUNCTION:                                                             �
�      Provides the write interface to the MPA device for both           �
�      user level and kernel level transmits.                            �
�                                                                        �
�  EXECUTION ENVIRONMENT:                                                �
�      mpa_write is part of a loadable device driver which is            �
�      reentrant and always pinned.  This routine cannot be called       �
�      from offlevel or interrupt level, it can only be called from      �
�      the process environment.                                          �
�                                                                        �
�  DATA STRUCTURES:                                                      �
�                                                                        �
�  RETURNS:                                                              �
�      0       Write succeeded.                                          �
�      n       Error number for error that occurred.                     �
�                                                                        �
�  EXTERNAL PROCEDURES CALLED: que_command,                              �
�                                                                        �  
藩様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様*/
int
mpa_write (
	dev_t           dev,                    /* device number */
	struct uio      *uiop,                  /* user I/O structure */
	int             chan,                   /* channel (always zero) */
	t_write_ext     *extptr,                /* write extension */
	unsigned int 	ignore)
{
	struct acb      *acb;                 /* ptr to adap. ctrl block */
	void 		xmit_timer();      /* failsafe transmit timer */
	t_write_ext     wr_ext;               /* write extension */
	xmit_elem_t *xmitp;
	dma_elem_t *dmap;
	dma_elem_t *tmp_dmap;
	struct mbuf *mbufp, *n, *freem = NULL; /* mbuf pointers */
	unsigned long   cio_wrt_flg;           /* temp flag for freeing mbufs*/
	int length;		    	       /* data length of write */
	int rc = 0;
	int fpl;
	int spl;
	caddr_t data_addr;			/* address of data in mbuf */
	int i;

	DDHKWD5( HKWD_DD_MPADD, DD_ENTRY_WRITE, 0, dev, 0, chan, extptr, 0 );
        MPATRACE4("WriE",dev,uiop->uio_iov->iov_base,uiop->uio_resid);

	if ( ((acb = get_acb(minor(dev))) == NULL) ||
		!(OPENP.op_flags & OP_OPENED) ||
		!(acb->flags & STARTED_CIO) ) 
	{
                MPATRACE2("Wre1",dev);
		return ENXIO;
	}

	if ((OPENP.op_mode&DWRITE) == 0) 
	{
           MPATRACE2("Wre2",dev);
	   return EACCES;
	}

	if(acb->flags&MPADEAD) 
	{
                MPATRACE2("Wre3",dev);
		return ENODEV;
	}

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	� handle the extension parameter �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	if (extptr != NULL) 
	{
		/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
		� get the caller-provided extension �
		青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
		rc = COPYIN(OPENP.op_mode , extptr, &wr_ext, sizeof(wr_ext));
		if (rc) 
		{
                        MPATRACE3("Wre4",dev,rc);
			return EFAULT;
		}
	}

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
	� cancel pending receive timeouts if it was set �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
    	if ( acb->strt_blk.rcv_timeout != 0 )
    	{
		untimeout( recv_timer, (caddr_t)acb);
		DISABLE_INTERRUPTS(fpl); 
		acb->flags &= ~RCV_TIMER_ON;
		ENABLE_INTERRUPTS(fpl); 
    	} 

	/*陳陳陳陳陳陳陳陳陳陳�
	� First, get the data �
	� into mbufs          �
	青陳陳陳陳陳陳陳陳陳�*/

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
	� this is a kernel process, the uio �
	� pointer is an mbuf pointer        �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
	mbufp = (struct mbuf *)(uiop->uio_iov->iov_base);

	if ( !M_INPAGE( mbufp ))                  /* crosses a page? */
        {
            return( EINVAL );                    /* then can't do it */
        }
	/*陳陳陳陳陳陳陳陳�
	� get data length �
	青陳陳陳陳陳陳陳�*/
        for ( n = mbufp, length = 0; n; n = n->m_next )
            length += n->m_len;
        if ( (length == 0) || ( length > PAGESIZE )) /* in bounds? */
        {
            return( EINVAL );                       /* bad length! */
        }

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	� Since we may have 2 mbufs, a caller mbuf �
	� and a local mbuf, cio_wrt_flg is used by � 
	� offlevel to see if mbuf (can be local or �
	� caller's mbuf) should be freed.  Use the �
	� writext.cio_write_flag in this routine to�
	� see if caller's mbuf should be freed.    �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	cio_wrt_flg = wr_ext.cio_write.flag;

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	� Check the "m_next" field to determine if �
	� this mbuf is chained or not.  If so, save�
	� the address of the mbuf since the unchain�
	� routine will return a new mbuf.	   �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	if ( mbufp->m_next != NULL )
	{
	     freem = mbufp;
	     mbufp = unchain_mbuf( acb, mbufp, length );
	     if ( mbufp == NULL )
	     {
	           return(EAGAIN);
	     }
	     else
	     {
		  /*陳陳陳陳陳陳陳陳陳陳陳陳陳�
		  � Always free the mbuf that �
		  � unchain_mbuf routine gets.�
		  青陳陳陳陳陳陳陳陳陳陳陳陳�*/
		  cio_wrt_flg &= ~CIO_NOFREE_MBUF;
	     }
	}

	/*陳陳陳陳陳陳陳陳陳陳陳�
	� Get addr of xfer data �
	青陳陳陳陳陳陳陳陳陳陳�*/
 	data_addr=MTOD(mbufp,caddr_t);

	DDHKWD3(HKWD_DD_MPADD,MPA_XMIT_DATA,0,dev,data_addr[0],data_addr[1]);

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
	� Now, get a transmit element �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
	xmitp = acb->xmit_free;
	if(xmitp == NULL) 
	{
	     /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	     � If there are no free transmit elements, wait for �
	     � one to free up (added for defect #92724).        �
	     青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	     if (rc = write_block(acb, uiop, &acb->xmitbuf_event))
	     {
                    MPATRACE3("Wre5",dev,rc);
	     	    mpalog(acb,ERRID_MPA_BFR, __LINE__, __FILE__,0,0);
		    wr_ext.cio_write.status = (ulong)CIO_TX_FULL;
		    return EAGAIN;
	     }
	     xmitp = acb->xmit_free; 
	     if(xmitp == NULL) 
	     {
                    MPATRACE3("Wre6",dev,rc);
	     	    mpalog(acb,ERRID_MPA_BFR, __LINE__, __FILE__,0,0);
		    wr_ext.cio_write.status = (ulong)CIO_TX_FULL;
		    return EAGAIN;
	     }
	}
        DISABLE_INTERRUPTS(fpl); 
	acb->xmit_free=xmitp->xm_next;
	bzero(xmitp,sizeof(xmit_elem_t));

	/*陳陳陳陳陳陳陳陳陳陳陳陳�
	� Put this element on the � 
	� active xmit q for this  �
	� adapter                 �
	青陳陳陳陳陳陳陳陳陳陳陳�*/
	if(acb->act_xmit_head==NULL) 
	{  /* its first one */
	    acb->act_xmit_head=xmitp;
	    acb->act_xmit_tail=xmitp;
	}
	else 
	{        /* its going on the end of a chain */
	    acb->act_xmit_tail->xm_next=xmitp;
	    acb->act_xmit_tail=xmitp;
	}

        ENABLE_INTERRUPTS(fpl);
	/*陳陳陳陳陳陳陳陳陳陳�
	� Tie the mbuf to the �
        � transmit pointer.   �
	青陳陳陳陳陳陳陳陳陳�*/

	xmitp->xm_mbuf = mbufp;

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
	� We have a valid transmit element, now �
        � initialize the transmit element       �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
	xmitp->xm_length = mbufp->m_len;
	xmitp->xm_netid = wr_ext.cio_write.netid;
	xmitp->xm_ack = cio_wrt_flg;
	xmitp->xm_writeid = wr_ext.cio_write.write_id;
	xmitp->xm_state |= XM_ACTIVE;

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
	� Check for poll/final bit and set xm_flags appropriately �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
	if( data_addr[POLL_BIT_OFFSET] & POLL_BIT )
	{
		xmitp->xm_flags |= XMIT_FINAL;	
	}

	/*陳陳陳陳陳陳陳陳陳朕
	� Now get a free DMA �
	� element from the q �
	青陳陳陳陳陳陳陳陳陳*/ 
	dmap = acb->dma_free; 
	if(dmap == NULL) 
	{
	     /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
	     � If there are no free DMA elements, wait for �
	     � one to free up (changed for defect #92724). �
	     青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
	     if (rc = write_block(acb, uiop, &acb->dmabuf_event)) 
	     {
                    MPATRACE3("Wre7",dev,rc);
	     	    mpalog(acb,ERRID_MPA_BFR, __LINE__, __FILE__,0,0);
		    free_xmit_elem(acb,xmitp);
		    wr_ext.cio_write.status = (ulong)CIO_TX_FULL;
		    return EAGAIN;
	     }
	     dmap = acb->dma_free; 
	     if(dmap == NULL) 
	     {
                    MPATRACE3("Wre8",dev,rc);
	     	    mpalog(acb,ERRID_MPA_BFR, __LINE__, __FILE__,0,0);
		    free_xmit_elem(acb,xmitp);
		    wr_ext.cio_write.status = (ulong)CIO_TX_FULL;
		    return EAGAIN;
	     }
	}
        DISABLE_INTERRUPTS(fpl);
	acb->dma_free=dmap->dm_next;
	bzero(dmap,sizeof(dma_elem_t));
        ENABLE_INTERRUPTS(fpl);

	/*陳陳陳陳陳陳陳陳陳陳陳陳朕
	� Now init the dma element �
	青陳陳陳陳陳陳陳陳陳陳陳陳*/

	dmap->p.xmit_ptr = xmitp;
	dmap->dm_buffer = MTOD(mbufp,caddr_t);
	dmap->dm_length = mbufp->m_len;
	dmap->dm_xmem = M_XMEMD(mbufp);
	dmap->dm_req_type = DM_XMIT;
	dmap->dm_state = DM_READY;
	dmap->dm_flags = DMA_NOHIDE;

        /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
        � Before I can set up this xmit dma, I must disable the  �
        � outstanding recv and d_complete the read d_slave call  �
        � even though the read has not yet occured. I will leave �
        � the DM_RECV dma element off the q and hold it.  Then   �
        � put it back on when there are no more xmits on the q.  �
        青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	if ( acb->flags & RECEIVER_ENABLED )
	{
         	if ( (rc = stoprecv(acb)) ) 
		{
                   	MPATRACE3("Wre9",dev,rc);
	       		free_dma_elem(acb,dmap);
	           	free_xmit_elem(acb,xmitp);
	           	return EAGAIN;
           	}
	} 

	if ( (rc = dma_request(acb,dmap)) ) 
	{
                MPATRACE3("WreA",dev,rc);
	        free_dma_elem(acb, dmap);
	        free_xmit_elem(acb, xmitp);
	        return EAGAIN;
	}

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	� Start a failsafe transmit timer, if not�
	� already started.                       �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	if( !(acb->flags & XMIT_TIMER_ON) )
	{
                MPATRACE4("Wri1",dev,acb->flags,acb->stats.ds.xmit_sent);
	        DISABLE_INTERRUPTS(fpl); 
		/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
		� this timer is being set on initial transmits and �
		� cancelled when the transmission of a frame with  �
                � poll/final bit has completed.  The timer duration�
		� was derived by considering the lowest line speed �
		� supported (600bps), the maximum frame size (4Kb) �
		� and maximum consecutive frame transmission (7).  �
		�						   �
		�                   (4096/600) * 7 = 49sec         �
		青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	  	timeout( xmit_timer, (caddr_t)acb, HZ * 49 );
		acb->flags |= XMIT_TIMER_ON;
	        ENABLE_INTERRUPTS(fpl);
	}
	++acb->stats.ds.xmit_sent;

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	� Check to see if "freem" points to� 
	� an mbuf and if we should free it �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	if ( freem && !( wr_ext.cio_write.flag & CIO_NOFREE_MBUF ))
        {
                MPATRACE4("Wri2",dev,rc,freem);
        	m_freem( freem );     /* free caller's mbuf */
        }

        MPATRACE2("WriX",dev);
	DDHKWD5( HKWD_DD_MPADD, DD_EXIT_WRITE, 0, dev, 0, 0, 0, 0 );
	return( 0 );
}
/**************************************************************************
**
** NAME:        write_block
**
** FUNCTION:
**
** EXECUTION ENVIRONMENT:
**
** NOTES:
**
** RETURNS:
**
*****************************************************************************/
int
write_block (
	struct acb *acb,
	struct uio *uiop,
	int *eventp)
{
	int spl;

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	� action will depend on whether DNDELAY is set �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	if (uiop->uio_fmode & DNDELAY) 
	{
		/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
		� set up flags so xmt_fn gets called �
		� when conditions change             �
		青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
		DISABLE_INTERRUPTS(spl);
		OPENP.op_flags |= XMIT_OWED;
		ENABLE_INTERRUPTS(spl);
		return EAGAIN;
	}
	/*陳陳陳陳陳陳陳陳陳�
	� block by sleeping �
	青陳陳陳陳陳陳陳陳�*/
	if (SLEEP(eventp) != EVENT_SUCC)
		return EINTR;
	return 0;
} /* write_block() */

/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
� xmit_timer - this routine is called when a transmit has not completed �
�	       within an acceptable timeframe to prevent a system hang.	�
青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
void xmit_timer(struct acb *acb)
{

        MPATRACE2("XtoE",acb->dev);
	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
	� log an error that timer expired �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
	mpalog(acb,ERRID_MPA_XFTO, __LINE__, __FILE__,0,0);

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
	� inform caller of transmit timeout �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
	async_status(acb, CIO_TX_DONE, MP_TX_FAILSAFE_TIMEOUT, 0, 0, 0);
	shutdown_adapter(acb);
	free_active_q(acb);

        MPATRACE2("XtoX",acb->dev);
	return;
}
