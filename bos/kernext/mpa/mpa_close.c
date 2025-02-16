static char sccsid[] = "@(#)82	1.7  src/bos/kernext/mpa/mpa_close.c, sysxmpa, bos41J, 9520B_all 5/17/95 14:53:38";
/*
 *   COMPONENT_NAME: (SYSXMPA) MP/A SDLC DEVICE DRIVER
 *
 *   FUNCTIONS: free_all_resources
 *		mpa_close
 *		
 *
 *   ORIGINS: 27
 *
 *   IBM CONFIDENTIAL -- (IBM Confidential Restricted when
 *   combined with the aggregated modules for this product)
 *                    SOURCE MATERIALS
 *
 *   (C) COPYRIGHT International Business Machines Corp. 1993, 1995
 *   All Rights Reserved
 *   US Government Users Restricted Rights - Use, duplication or
 *   disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#include <errno.h>
#include <sys/adspace.h>
#include <sys/device.h>
#include <sys/ddtrace.h>
#include <sys/ioacc.h>
#include <sys/iocc.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/timer.h>
#include <sys/sleep.h>
#include <sys/mpadd.h>
#include <sys/sysmacros.h>
#include <sys/trchkid.h>


 /*様様様様様様様様様様様様様様様� MPA_CLOSE 様様様様様様様様様様様様様様様融
 � NAME: mpa_close                                                          �
 �                                                                          �
 � FUNCTION: Called to close a single port of the multiprotocol             �
 �           adaptor.                                                       �
 �                                                                          �
 � EXECUTION ENVIRONMENT:                                                   �
 �                                                                          �
 �      Preemptable        : Yes                                            �
 �      VMM Critical Region: Yes                                            �
 �      Runs on Fixed Stack: Yes                                            �
 �      May Page Fault     : No                                             �
 �                                                                          �
 � (NOTES:) More detailed description of the function, down to              �
 �      what bits / data structures, etc it manipulates.                    �
 �                                                                          �
 � (RECOVERY OPERATION:) Information describing both hardware and           �
 �      software error recovery.                                            �
 �                                                                          �
 � (DATA STRUCTURES:) Effects on global data structures, similar to NOTES.  �
 �                                                                          �
 � RETURNS: What this code returns (NONE, if it has no return value)        �
 藩様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様*/

int mpa_close ( dev_t            dev,
		int             chan,
		int             ext )

{
	int             spl, rc;
	struct acb      *acb;         /* pointer to adapter control block */

	/*陳陳陳陳陳陳陳陳朕
	� log a trace hook �
	青陳陳陳陳陳陳陳陳*/
	DDHKWD5(HKWD_DD_MPADD,DD_ENTRY_CLOSE,0,dev,chan,ext,0,0);
	MPATRACE4("ClsE",dev,chan,ext);

	if ((acb = get_acb(minor(dev))) == NULL) 
	{
		MPATRACE2("Cle1",dev);
		return ENODEV;
	}

	if (lockl(&acb->adap_lock, LOCK_SIGRET) != LOCK_SUCC) 
	{
		MPATRACE2("Cle2",dev);
		return EINTR;
	}


	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	� now we follow down to remove the select queue data structure �
	� the channel information data structure and null out the      �
	� pointer in the dds to the channel info ds                    �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/

	DISABLE_INTERRUPTS(spl);

	if /* close being called before CIO_HALT */
	( acb->flags & STARTED_CIO )
	{
		/* Defect 178174 */
		acb->flags &= ~STARTED_CIO; /* mark as not started */
		/* End Defect 178174 */
		if( (rc=shutdown_adapter(acb)) ) 
		{
			MPATRACE3("Cle3",dev,rc);
			ENABLE_INTERRUPTS(spl);
          		return rc;
     		}

     		free_active_q(acb);
	}
	ENABLE_INTERRUPTS(spl);

	OPENP.mpa_kopen.rx_fn = NULL;
	OPENP.mpa_kopen.stat_fn = NULL;
	OPENP.mpa_kopen.tx_fn = NULL;
	OPENP.mpa_kopen.open_id = 0;

	MPATRACE3("ClsX",dev,chan);

        free_open_struct(acb);

	OPENP.op_mode = 0;      /* remove device flags */

	acb->flags &= ~STARTED_CIO; /* mark as not started */

	unlockl(&acb->adap_lock);

	DDHKWD5(HKWD_DD_MPADD,DD_EXIT_CLOSE,0,dev,chan,ext,0,0);
	return(0);
} /* mpa_close() */

/*
 * Note that this routine is only called from mpa_term_dev which
 * implies that it is always called with the adapter lock held and
 * interrupts enabled.  Since the first thing done is to shutdown the
 * adapter, interrupts are not disabled when all the resources are
 * freed.  This makes the freeing easier.
 */
void
free_all_resources (struct acb *acb)
{
  irpt_elem_t   *irptp;
  irpt_elem_t   *next_ip;
  recv_elem_t   *recvp;
  recv_elem_t   *next_rp;
  xmit_elem_t   *xmitp;
  xmit_elem_t   *next_xp;
  dma_elem_t    *dmap;
  dma_elem_t    *next_dp;
  int           one_started = 0;


  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
  � prior to freeing all the resources I want to make sure I get �
  � no more irpts from the adapter, so I am shuting it down here.� 
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
  shutdown_adapter(acb);

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
  � free all dma elements on active list �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
  if ( (dmap=acb->act_dma_head) != NULL) 
  {
     while (dmap) 
     {
	next_dp = dmap->dm_next;

	/*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
	� if DMA_STARTED already do d_complete �
	青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
	if(dmap->dm_state == DM_STARTED && !one_started) 
	{
	   one_started= 1;
	   d_complete(acb->dma_channel, dmap->dm_flags,
		dmap->dm_buffer, dmap->dm_length,
		dmap->dm_xmem, NULL);
	}
	KFREE(dmap);
	dmap = next_dp;
     }
     acb->act_dma_head = acb->act_dma_tail = NULL;
  }

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � Now that I know there are no dmas active d_clear dma channel. �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  if (acb->flags & MPADMACHAN) 
  {
	  d_clear(acb->dma_channel);
  }
  acb->flags &= ~MPADMACHAN;


  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
  � free all dma elements on free list �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
  if ( (dmap=acb->dma_free) != NULL) 
  {
     while (dmap) 
     {
	next_dp = dmap->dm_next;
	KFREE(dmap);
	dmap = next_dp;
     }
     acb->dma_free = NULL;
  }


  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � free all irpt elements on active list �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  if ( (irptp=acb->act_irpt_head) != NULL) 
  {
     while (irptp) 
     {
	next_ip = irptp->ip_next;
	KFREE(irptp);
	irptp = next_ip;
     }
     acb->act_irpt_head = acb->act_irpt_tail = NULL;
  }

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � Now that I know there are no irpts comming i_clear irpt struct(s) �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  if (acb->flags & MPAIINSTALL3) 
  {
	 i_clear(&acb->ih_structA);
  }
  acb->flags &= ~MPAIINSTALL3;

  if (acb->flags & MPAIINSTALL4) 
  {
	 i_clear(&acb->ih_structB);
  }
  acb->flags &= ~MPAIINSTALL4;

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � free all irpt elements on free list �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  if ( (irptp=acb->irpt_free) != NULL) 
  {
     while (irptp) 
     {
	next_ip = irptp->ip_next;
	KFREE(irptp);
	irptp = next_ip;
     }
     acb->irpt_free = NULL;
  }

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � free all xmit elements on active list �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  if ( (xmitp=acb->act_xmit_head) != NULL) 
  {
     while (xmitp) 
     {
	next_xp = xmitp->xm_next;
	free_xmit_elem(acb,xmitp);
	xmitp = next_xp;
     }
     acb->act_xmit_head = acb->act_xmit_tail = NULL;
  }

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � free all xmit elements on free list �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  if ( (xmitp=acb->xmit_free) != NULL) 
  {
     while (xmitp) 
     {
	next_xp = xmitp->xm_next;
	KFREE(xmitp);
	xmitp = next_xp;
     }
     acb->xmit_free = NULL;
  }

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � free all recv elements on active list �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  if ( (recvp=acb->act_recv_head) != NULL) 
  {
     while (recvp) 
     {
	next_rp = recvp->rc_next;
        /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳朕
        � free any unused MBUFs at this time �
        � (this was added for defect #091440)�
        青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳*/
        if (recvp->rc_state & RC_MBUF)
        {
                m_freem((struct mbuf *)recvp->rc_mbuf);
                recvp->rc_state &= ~RC_MBUF;
        }
	KFREE(recvp);
	recvp = next_rp;
     }
     acb->act_recv_head = acb->act_recv_tail = NULL;
  }

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � free all recv elements on free list �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  if ( (recvp=acb->recv_free) != NULL) 
  {
     while (recvp) 
     {
	next_rp = recvp->rc_next;
	KFREE(recvp);
	recvp = next_rp;
     }
     acb->recv_free = NULL;
  }

  /*陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�
  � Clear the acb flags not cleared in other locations. �
  青陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳陳�*/
  acb->flags &= ~NEED_IRPT_ELEM;
  acb->flags &= ~NEED_RECV_ELEM;
  acb->flags &= ~NEED_DMA_ELEM;
  acb->flags &= ~RECV_DMA_ON_Q;
  acb->flags &= ~STARTED_CIO;

  return;
}         /* free_all_resources() */
