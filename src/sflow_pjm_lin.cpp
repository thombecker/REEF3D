/*--------------------------------------------------------------------
REEF3D
Copyright 2008-2025 Hans Bihs

This file is part of REEF3D.

REEF3D is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
--------------------------------------------------------------------
Author: Hans Bihs
--------------------------------------------------------------------*/

#include"sflow_pjm_lin.h"
#include"lexer.h"
#include"fdm2D.h" 
#include"ghostcell.h"
#include"poisson.h"
#include"solver2D.h"
#include"momentum.h"
#include"ioflow.h"
#include"patchBC_interface.h"

#define HP (fabs(b->hp(i,j))>1.0e-20?b->hp(i,j):1.0e20)

#define HPIP (fabs(b->hp(i+1,j))>1.0e-20?b->hp(i+1,j):1.0e20)
#define HPJP (fabs(b->hp(i,j+1))>1.0e-20?b->hp(i,j+1):1.0e20)

#define HPXP (0.5*(HP + HPIP))
#define HPYP (0.5*(HP + HPJP))

sflow_pjm_lin::sflow_pjm_lin(lexer* p, fdm2D *b, patchBC_interface *ppBC) 
{
    pBC = ppBC;
    
    gcval_press=40;  
	
	gcval_u=10;
	gcval_v=11;
	gcval_w=12;

    wd_criterion=p->A244;
}

sflow_pjm_lin::~sflow_pjm_lin()
{
}

void sflow_pjm_lin::start(lexer *p, fdm2D *b, ghostcell *pgc, solver2D *psolv, ioflow *pflow, slice &P, slice &Q, slice &Pn, slice &Qn, slice &ws, slice &eta, double alpha)
{
    if(p->mpirank==0 && (p->count%p->P12==0))
    cout<<".";
	
	starttime=pgc->timer();
	
    
    rhs(p,b,P,Q,ws,alpha);
	pgc->gcsl_start4(p,b->press,gcval_press);
	
    poisson(p,b,alpha);
	
        solvtime=pgc->timer();

    psolv->start(p,pgc,b->press,b->M,b->xvec,b->rhsvec,4);
	
        p->poissontime=pgc->timer()-solvtime;
  
    pflow->pm_relax(p,pgc,b->press);
	pgc->gcsl_start4(p,b->press,gcval_press);    
  
	ucorr(p,b,P,eta,alpha);
	vcorr(p,b,Q,eta,alpha);
    wcorr(p,b, alpha, P, Q, ws);
	pgc->gcsl_start4(p,ws,12);

    p->poissoniter=p->solveriter;

	ptime=pgc->timer()-starttime;

	if(p->mpirank==0 && (p->count%p->P12==0) && (p->count%p->P12==0))
	cout<<"piter: "<<p->solveriter<<"  solvtime: "<<setprecision(3)<<p->poissontime<<"  ptime: "<<setprecision(3)<<ptime<<endl;
}

void sflow_pjm_lin::ucorr(lexer* p, fdm2D* b, slice& P, slice &eta, double alpha)
{	
	SLICELOOP1
    WETDRY1
    if(b->breaking(i,j)==0 && b->breaking(i+1,j)==0)
	P(i,j) += -alpha*p->dt*(((b->press(i+1,j)-b->press(i,j))/(p->DXM*p->W1)))
           
             + alpha*p->dt*((b->press(i+1,j)+b->press(i,j))*(b->depth(i+1,j)-b->depth(i,j))     
				             /(p->DXM*HPXP*p->W1));
}

void sflow_pjm_lin::vcorr(lexer* p, fdm2D* b, slice& Q, slice &eta, double alpha)
{	
	SLICELOOP2
    WETDRY2
    if(b->breaking(i,j)==0 && b->breaking(i,j+1)==0)
	Q(i,j) += -alpha*p->dt*(((b->press(i,j+1)-b->press(i,j))/(p->DXM*p->W1)))
                
             + alpha*p->dt*((b->press(i,j+1)+b->press(i,j))*(b->depth(i,j+1)-b->depth(i,j))
                            /(p->DXM*HPYP*p->W1));
}

void sflow_pjm_lin::wcorr(lexer* p, fdm2D* b, double alpha, slice &P, slice &Q, slice &ws)
{	    
    SLICELOOP4
    WETDRY
    if(b->breaking(i,j)==0)
	ws(i,j) += p->dt*alpha*(2.0*b->press(i,j)/(HP*p->W1));
}

void sflow_pjm_lin::wcalc(lexer* p, fdm2D* b,double alpha, slice &P, slice &Q, slice &ws)
{	
}

void sflow_pjm_lin::rhs(lexer *p, fdm2D* b, slice &P, slice &Q, slice &w, double alpha)
{
    NSLICELOOP4
	b->rhsvec.V[n]=0.0;
    
	count=0;
    SLICELOOP4
    {
    b->rhsvec.V[count] =   -((P(i,j) - P(i-1,j))*(b->hp(i,j))
                           + (Q(i,j) - Q(i,j-1))*(b->hp(i,j)))/(alpha*p->dt*p->DXM)
                           
                           -2.0*(w(i,j) + 0.25*(P(i,j)+P(i-1,j))*(b->depth(i+1,j)-b->depth(i-1,j))/p->DXM
                                        + 0.25*(Q(i,j)+Q(i,j-1))*(b->depth(i,j+1)-b->depth(i,j-1))/p->DXM
                           
                           )   /(alpha*p->dt);
                           
    ++count;
    
    b->press(i,j)=0.0;
    }
}

void sflow_pjm_lin::poisson(lexer*p, fdm2D* b, double alpha)
{
    sqd = (1.0/(p->DXM*p->DXM*p->W1));
    
    double fac=1.0;
    
    
    n=0;
    SLICELOOP4
	{
	b->M.p[n]  =  (b->hp(i,j)*sqd + b->hp(i,j)*sqd)*p->x_dir
               +  (b->hp(i,j)*sqd + b->hp(i,j)*sqd)*p->y_dir + 2.0/(HP*p->W1);     
               

   	b->M.n[n] = -b->hp(i,j)*sqd*p->x_dir;
	b->M.s[n] = -b->hp(i,j)*sqd*p->x_dir;

	b->M.w[n] = -b->hp(i,j)*sqd*p->y_dir;
	b->M.e[n] = -b->hp(i,j)*sqd*p->y_dir;

	++n;
	}
	
	
    n=0;
	SLICELOOP4
	{
        // Inflow
		if(p->flagslice4[Im1J]<0 && p->IOSL[Im1J]==1)
		{
		b->rhsvec.V[n] -= 0.0;
		b->M.s[n] = 0.0;
		}
        
        if(p->flagslice4[Im1J]<0 && p->IOSL[Im1J]==0)
		{
		b->rhsvec.V[n] -= b->M.s[n]*b->press(i-1,j);
		b->M.s[n] = 0.0;
		}
		
         // Outflow
		if(p->flagslice4[Ip1J]<0 && p->IOSL[Ip1J]==1)
		{
		b->rhsvec.V[n] -= 0.0;
		b->M.n[n] = 0.0;
		}
        
        if(p->flagslice4[Ip1J]<0 && p->IOSL[Ip1J]==0)
		{
		b->rhsvec.V[n] -= b->M.n[n]*b->press(i+1,j);
		b->M.n[n] = 0.0;
		}
		
		if(p->flagslice4[IJm1]<0)
		{
		b->rhsvec.V[n] -= b->M.e[n]*b->press(i,j-1);
		b->M.e[n] = 0.0;
		}
		
		if(p->flagslice4[IJp1]<0)
		{
		b->rhsvec.V[n] -= b->M.w[n]*b->press(i,j+1);
		b->M.w[n] = 0.0;
		}
		
	++n;
	}
    
    n=0;
    SLICELOOP4
	{
        if(p->wet[IJ]==0 || p->wet[Im1J]==0 || p->wet[Ip1J]==0 || p->wet[IJm1]==0 || p->wet[IJp1]==0 || b->breaking(i,j)==1)
        {
        b->M.p[n]  = 1.0;

        b->M.n[n] = 0.0;
        b->M.s[n] = 0.0;

        b->M.w[n] = 0.0;
        b->M.e[n] = 0.0;
        
        b->rhsvec.V[n]=0.0;
        }
	++n;
	}
}

void sflow_pjm_lin::upgrad(lexer*p, fdm2D* b, slice &eta, slice &eta_n)
{

    SLICELOOP1
    WETDRY1
    b->F(i,j) -= fabs(p->W22)*(p->A223*eta(i+1,j) + (1.0-p->A223)*eta_n(i+1,j) 
                                     - p->A223*eta(i,j) - (1.0-p->A223)*eta_n(i,j) )/(p->DXM);

        
        if(p->B77==10)
        for(n=0;n<p->gcslout_count;n++)
        {
        i=p->gcslout[n][0]-1;
        j=p->gcslout[n][1];
        
        WETDRY1
        {
        b->F(i,j) += fabs(p->W22)*(p->A223*eta(i+1,j) + (1.0-p->A223)*eta_n(i+1,j) 
                                     - p->A223*eta(i,j) - (1.0-p->A223)*eta_n(i,j) )/(p->DXM);
                                     
        b->F(i,j) -= fabs(p->W22)*(p->A223*(b->bed(i,j)-p->wd) + (1.0-p->A223)*(b->bed(i,j)-p->wd)
                                     - p->A223*eta(i,j) - (1.0-p->A223)*eta_n(i,j) )/(p->DXM);
        }
                                     
        }  


    pBC->patchBC_pressure2D_ugrad(p,b,eta,eta_n);
}

void sflow_pjm_lin::vpgrad(lexer*p, fdm2D* b, slice &eta, slice &eta_n)
{
        SLICELOOP2
        WETDRY2
        b->G(i,j) -= fabs(p->W22)*(p->A223*eta(i,j+1) + (1.0-p->A223)*eta_n(i,j+1) 
                                 - p->A223*eta(i,j) - (1.0-p->A223)*eta_n(i,j) )/(p->DXM); 
                                 
        pBC->patchBC_pressure2D_vgrad(p,b,eta,eta_n);
}

void sflow_pjm_lin::wpgrad(lexer*p, fdm2D* b, slice &eta, slice &eta_n)
{
    SLICELOOP4
    b->L(i,j)=0.0;	    
}
