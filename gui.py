"""HP 4145A GUI v1.5: current/time, voltage/time and I-V sweep.
Run with: python gui.py
Requires tkinter, matplotlib and local ugpibd --enable-prologix.
"""
from __future__ import annotations
import csv
import queue
import threading
import time
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
from hp4145a import HP4145AError
from measurement_modes import MeasurementSession, build_commands, effective_config, IV_FIXED

MODES = ('Current vs Time', 'Voltage vs Time', 'I–V Sweep')

class AnalyzerGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title('HP 4145A — Measurement Control (v1.5)')
        self.geometry('1180x820')
        self.minsize(870,630)
        self.running = False
        self.events = queue.Queue()
        self._last_data = None
        self._last_non_iv = None
        self._previous_mode = 'Current vs Time'
        def s(value): return tk.StringVar(value=str(value))
        self.measurement = s(MODES[0]); self.address = s(16)
        self.smu1_mode = s('V'); self.smu1_function = s('CONST'); self.smu1_source = s(0)
        self.smu3_mode = s('COM'); self.smu3_function = s('CONST'); self.smu3_source = s(0)
        self.v1=s('V1'); self.i1=s('I1'); self.v3=s('V3'); self.i3=s('I3')
        self.current_limit=s(50); self.voltage_limit=s(1)
        self.wait=s(1); self.interval=s(2); self.points=s(15)
        self.sweep_start=s(0); self.sweep_stop=s(.5); self.sweep_step=s(.01)
        self.integration=s('Unchanged'); self.magnitude=tk.BooleanVar(value=False)
        # Display page settings: editable per mode, independent of source compliance.
        self.apply_axes=tk.BooleanVar(value=False)
        self.axis_x=s('TIME'); self.axis_y1=s('I1'); self.axis_y2=s('V1')
        self.axis_x_min=s(0); self.axis_x_max=s(30)
        self.axis_y1_min=s(-.05); self.axis_y1_max=s(0)
        self.axis_y2_min=s(0); self.axis_y2_max=s(.5)
        self.axis_y2_enabled=tk.BooleanVar(value=False)
        self.status=s('Idle')
        self._build()
        self._change_measurement()
        self.after(100,self._process_events)

    def entry(self, parent, row, label, var, width=14):
        ttk.Label(parent,text=label).grid(row=row,column=0,sticky='w',padx=6,pady=4)
        w=ttk.Entry(parent,textvariable=var,width=width)
        w.grid(row=row,column=1,sticky='w',padx=6,pady=4)
        return w

    def combo(self,parent,row,label,var,options):
        ttk.Label(parent,text=label).grid(row=row,column=0,sticky='w',padx=6,pady=4)
        w=ttk.Combobox(parent,textvariable=var,values=options,state='readonly',width=17)
        w.grid(row=row,column=1,sticky='w',padx=6,pady=4)
        return w

    def _build(self):
        h=ttk.Frame(self,padding=(12,9));h.pack(fill='x')
        ttk.Label(h,text='HP 4145A — Measurement Control',font=('Helvetica',17,'bold')).pack(anchor='w')
        ttk.Label(h,text='SMU1 source/monitor • SMU3 common • other channels disabled').pack(anchor='w')
        toolbar=ttk.Frame(self,padding=(12,5)); toolbar.pack(fill='x')
        self.run_btn=ttk.Button(toolbar,text='▶ Run Measurement',command=self.start)
        self.run_btn.pack(side='left',padx=(0,8))
        self.preview_btn=ttk.Button(toolbar,text='Preview Commands',command=self.preview)
        self.preview_btn.pack(side='left',padx=(0,8))
        self.auto_plot_btn=ttk.Button(toolbar,text='Auto Scale Plot',command=self.autoscale_plot)
        self.auto_plot_btn.pack(side='left',padx=(0,8))
        self.auto_analyzer_btn=ttk.Button(toolbar,text='Fit Analyzer Axes',command=self.autoscale_analyzer)
        self.auto_analyzer_btn.pack(side='left',padx=(0,12))
        ttk.Label(toolbar,textvariable=self.status).pack(side='left')
        main=ttk.Frame(self,padding=(12,5,12,12));main.pack(fill='both',expand=True)
        leftbox=ttk.Frame(main,width=385);leftbox.pack(side='left',fill='y',padx=(0,12))
        leftbox.pack_propagate(False)
        canvas=tk.Canvas(leftbox,highlightthickness=0,borderwidth=0)
        sc=ttk.Scrollbar(leftbox,orient='vertical',command=canvas.yview)
        canvas.configure(yscrollcommand=sc.set)
        canvas.pack(side='left',fill='both',expand=True);sc.pack(side='right',fill='y')
        left=ttk.Frame(canvas);win=canvas.create_window((0,0),window=left,anchor='nw')
        left.bind('<Configure>',lambda e:canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>',lambda e:canvas.itemconfigure(win,width=e.width))
        right=ttk.Frame(main);right.pack(side='left',fill='both',expand=True)

        setup=ttk.LabelFrame(left,text='Measurement',padding=8);setup.pack(fill='x',pady=4)
        self.entry(setup,0,'GPIB address',self.address)
        self.combo(setup,1,'Type',self.measurement,MODES)
        self.measurement.trace_add('write',lambda *_:self._change_measurement())

        c1=ttk.LabelFrame(left,text='SMU1 — source / monitor',padding=8);c1.pack(fill='x',pady=4)
        self.v1_entry=self.entry(c1,0,'Voltage name',self.v1)
        self.i1_entry=self.entry(c1,1,'Current name',self.i1)
        self.smu1_mode_widget=self.combo(c1,2,'Mode',self.smu1_mode,('V','I','COM','OFF'))
        self.smu1_function_widget=self.combo(c1,3,'Function',self.smu1_function,('CONST','VAR1'))
        self.source_entry=self.entry(c1,4,'Source value (V/A)',self.smu1_source)
        self.current_limit_entry=self.entry(c1,5,'I compliance (mA)',self.current_limit)
        self.entry(c1,6,'V compliance (V)',self.voltage_limit)
        c3=ttk.LabelFrame(left,text='SMU3 — common',padding=8);c3.pack(fill='x',pady=4)
        self.v3_entry=self.entry(c3,0,'Voltage name',self.v3)
        self.i3_entry=self.entry(c3,1,'Current name',self.i3)
        self.smu3_mode_widget=self.combo(c3,2,'Mode',self.smu3_mode,('COM','V','I','OFF'))
        self.smu3_function_widget=self.combo(c3,3,'Function',self.smu3_function,('CONST','VAR1'))
        self.smu3_source_entry=self.entry(c3,4,'Source value (V/A)',self.smu3_source)
        ttk.Label(c3,text='SMU2/4, VS1/2, VM1/2: disabled',foreground='gray').grid(row=5,column=0,columnspan=2,sticky='w',pady=4)
        self.timeframe=ttk.LabelFrame(left,text='Time-domain settings',padding=8)
        self.entry(self.timeframe,0,'Wait (s)',self.wait)
        self.entry(self.timeframe,1,'Interval (s)',self.interval)
        self.entry(self.timeframe,2,'Readings',self.points)
        self.sweepframe=ttk.LabelFrame(left,text='I–V linear sweep settings',padding=8)
        self.sweep_widgets=[
            self.entry(self.sweepframe,0,'Start voltage (V)',self.sweep_start),
            self.entry(self.sweepframe,1,'Stop voltage (V)',self.sweep_stop),
            self.entry(self.sweepframe,2,'Step voltage (V)',self.sweep_step),
        ]
        ttk.Label(self.sweepframe,text='Fixed I–V profile: 0–0.5 V, 0.005 V step; 100 mA compliance.',
                  wraplength=330,foreground='gray').grid(row=3,column=0,columnspan=2,
                  sticky='w',padx=6,pady=(4,2))
        # Display page is placed before integration; every field is user-reviewable.
        disp=ttk.LabelFrame(left,text='Analyzer Display Setup (X / Y1 / Y2)',padding=8)
        disp.pack(fill='x',pady=4)
        self.apply_axes_widget=ttk.Checkbutton(disp,text='Send axis settings to analyzer',variable=self.apply_axes)
        self.apply_axes_widget.grid(row=0,column=0,columnspan=3,sticky='w',padx=6,pady=4)
        self.axis_box=disp
        self.axis_fixed_widgets=[
            self.combo(disp,1,'X axis name',self.axis_x,('TIME','V1','I1','V3','I3')),
            self.entry(disp,2,'X min (s or V)',self.axis_x_min),
            self.entry(disp,3,'X max (s or V)',self.axis_x_max),
            self.combo(disp,4,'Y1 axis name',self.axis_y1,('I1','V1','I3','V3')),
            self.entry(disp,5,'Y1 min (A or V)',self.axis_y1_min),
            self.entry(disp,6,'Y1 max (A or V)',self.axis_y1_max),
        ]
        self.axis_y2_widget=ttk.Checkbutton(disp,text='Enable optional Y2 axis',variable=self.axis_y2_enabled)
        self.axis_y2_widget.grid(row=7,column=0,columnspan=3,sticky='w',padx=6,pady=4)
        self.axis_y2_name_widget=self.combo(disp,8,'Y2 axis name',self.axis_y2,('V1','I1','V3','I3'))
        self.axis_y2_min_widget=self.entry(disp,9,'Y2 min (A or V)',self.axis_y2_min)
        self.axis_y2_max_widget=self.entry(disp,10,'Y2 max (A or V)',self.axis_y2_max)
        ttk.Button(disp,text='Load preset for selected measurement',command=self.load_axis_preset).grid(
            row=11,column=0,columnspan=3,sticky='w',padx=6,pady=7)
        ttk.Label(disp,text='Axis limits change the analyzer display, not source compliance.',
                  foreground='gray',wraplength=330).grid(row=12,column=0,columnspan=3,sticky='w',padx=6,pady=3)
        other=ttk.LabelFrame(left,text='Display and integration',padding=8);other.pack(fill='x',pady=4)
        self.other_frame = other
        self.combo(other,0,'Integration',self.integration,('Unchanged','SHORT','MEDIUM','LONG'))
        ttk.Checkbutton(other,text='Plot current magnitude (absolute value)',variable=self.magnitude).grid(row=1,column=0,columnspan=2,sticky='w',padx=6,pady=4)
        ttk.Label(left,text='Modes other than the tested current workflow require\nseparate electrical verification before Run.',foreground='gray').pack(anchor='w',pady=8)

        self.fig=Figure(figsize=(7,4.6),dpi=100)
        self.ax=self.fig.add_subplot(111)
        self.ax.set(title='Current vs Time',xlabel='Time (s)',ylabel='Current (mA)');self.ax.grid(True,alpha=.3)
        self.chart=FigureCanvasTkAgg(self.fig,master=right)
        self.chart.get_tk_widget().pack(fill='both',expand=True)
        self.log=tk.Text(right,height=9,wrap='word',state='disabled')
        self.log.pack(fill='x',pady=(5,0))
        self._log("Ready. I–V uses a fixed 100 mA source-compliance setting. This is the instrument maximum, not protection against a 300 mA-rated solar panel.")
        self._log('Auto Scale Plot affects only Mac graph. Fit Analyzer Axes sends display-axis commands after a completed acquisition.')
        self._iv_lock_widgets = [self.v1_entry,self.i1_entry,self.v3_entry,self.i3_entry,
                                 self.smu1_mode_widget,self.smu1_function_widget,
                                 self.smu3_mode_widget,self.smu3_function_widget,
                                 self.smu3_source_entry,self.source_entry,
                                 self.current_limit_entry,*self.sweep_widgets,
                                 self.apply_axes_widget,self.axis_y2_widget,
                                 self.axis_y2_name_widget,self.axis_y2_min_widget,
                                 self.axis_y2_max_widget,*self.axis_fixed_widgets]

    def _change_measurement(self):
        if not hasattr(self,'timeframe'):return
        mode=self.measurement.get()
        old=self._previous_mode
        if old != 'I–V Sweep' and mode == 'I–V Sweep':
            # Preserve the previous working configuration when entering I–V.
            self._last_non_iv={
                key:getattr(self,key).get() for key in
                ('v1','i1','v3','i3','current_limit','smu1_source','smu3_source')
            }
        elif old == 'I–V Sweep' and mode != 'I–V Sweep' and self._last_non_iv:
            for key,value in self._last_non_iv.items():
                getattr(self,key).set(value)
        self._previous_mode=mode
        self.timeframe.pack_forget();self.sweepframe.pack_forget()
        if mode=='I–V Sweep':
            self.sweepframe.pack(fill='x',pady=4,before=self.other_frame)
            for key,value in IV_FIXED.items():
                name={
                    'current_limit_a':'current_limit'
                }.get(key,key)
                if not hasattr(self,name):continue
                variable=getattr(self,name)
                if name=='current_limit':value=value*1000
                if isinstance(variable,(tk.StringVar,tk.BooleanVar)):
                    variable.set(value)
            self.load_axis_preset()
            self._log('I–V locked preset loaded. X display ends at 0.1 V, although the voltage sweep runs to 0.5 V.')
        else:
            self.timeframe.pack(fill='x',pady=4,before=self.other_frame)
            self.smu1_mode.set('V' if mode=='Current vs Time' else 'I')
            self.smu1_function.set('CONST')
            self.smu1_source.set('0')
            self.load_axis_preset()
        for widget in self._iv_lock_widgets:
            widget.configure(state='disabled' if mode=='I–V Sweep' else
                             ('readonly' if isinstance(widget,ttk.Combobox) else 'normal'))
        self.ax.clear()
        self.ax.set(title=mode,xlabel='Voltage (V)' if mode=='I–V Sweep' else 'Time (s)',
                    ylabel='Voltage (V)' if mode=='Voltage vs Time' else 'Current (mA)')
        self.ax.grid(True,alpha=.3);self.chart.draw_idle()

    def load_axis_preset(self):
        """Populate display values without sending any instrument commands."""
        mode=self.measurement.get()
        self.axis_y2_enabled.set(False)
        if mode == 'Current vs Time':
            vals=('TIME',0,30,'I1',-.05,0,'V1',0,.5)
            self.apply_axes.set(False)  # Preserve working acquisition.
        elif mode == 'Voltage vs Time':
            vals=('TIME',0,30,'V1',0,.5,'I1',-.05,0)
            self.apply_axes.set(True)
        else:
            # X minimum was unspecified (a dash), so use 0 V. Note that this
            # requested 0.1 V max crops 0.1–0.5 V on the physical analyzer.
            vals=('V1',0,.1,'I1',-.1,1.0,'V1',0,.5)
            self.apply_axes.set(True)
        for var,val in zip((self.axis_x,self.axis_x_min,self.axis_x_max,
                            self.axis_y1,self.axis_y1_min,self.axis_y1_max,
                            self.axis_y2,self.axis_y2_min,self.axis_y2_max),vals):
            var.set(str(val))

    def _log(self,msg):
        self.log.configure(state='normal');self.log.insert('end',msg+'\n')
        self.log.see('end');self.log.configure(state='disabled')

    def settings(self):
        def n(var):
            v=float(var.get())
            import math
            if not math.isfinite(v):raise ValueError('All numeric inputs must be finite')
            return v
        cfg=dict(measurement=self.measurement.get(),address=int(self.address.get()),
                 smu1_mode=self.smu1_mode.get(),smu1_function=self.smu1_function.get(),
                 smu1_source=n(self.smu1_source),smu3_mode=self.smu3_mode.get(),
                 smu3_function=self.smu3_function.get(),smu3_source=n(self.smu3_source),
                 v1=self.v1.get().strip().upper(),i1=self.i1.get().strip().upper(),
                 v3=self.v3.get().strip().upper(),i3=self.i3.get().strip().upper(),
                 current_limit_a=n(self.current_limit)/1000,voltage_limit_v=n(self.voltage_limit),
                 wait=n(self.wait),interval=n(self.interval),points=int(self.points.get()),
                 sweep_start=n(self.sweep_start),sweep_stop=n(self.sweep_stop),
                 sweep_step=n(self.sweep_step),integration=self.integration.get(),
                 magnitude=self.magnitude.get(),
                 apply_axes=self.apply_axes.get(),
                 axis_x=self.axis_x.get().strip().upper(),
                 axis_y1=self.axis_y1.get().strip().upper(),
                 axis_y2=self.axis_y2.get().strip().upper(),
                 axis_x_min=n(self.axis_x_min),axis_x_max=n(self.axis_x_max),
                 axis_y1_min=n(self.axis_y1_min),axis_y1_max=n(self.axis_y1_max),
                 axis_y2_min=n(self.axis_y2_min),axis_y2_max=n(self.axis_y2_max),
                 axis_y2_enabled=self.axis_y2_enabled.get())
        if not 0<=cfg['address']<=30:raise ValueError('GPIB address must be 0–30')
        if cfg['smu3_source']!=0:raise ValueError('SMU3 COM source value must remain 0')
        cfg=effective_config(cfg)
        build_commands(cfg)
        return cfg

    def preview(self):
        try:
            commands,record,expected=build_commands(self.settings())
            self._log('OFFLINE COMMAND PREVIEW (nothing sent):\n  '+'\n  '.join(commands))
            self._log(f'Expected {expected} samples; read {record}.')
        except Exception as exc:
            messagebox.showerror('Invalid configuration',str(exc))

    def start(self):
        if self.running:return
        try:
            cfg=self.settings()
        except Exception as exc:
            messagebox.showerror('Invalid configuration',str(exc));return
        commands, _, expected = build_commands(cfg)
        summary = (f"Measurement: {cfg['measurement']}\n"
                   f"SMU1: {cfg['smu1_mode']} / {cfg['smu1_function']}\n"
                   f"SMU3: COM / CONST\n"
                   f"Current compliance: {cfg['current_limit_a']*1000:g} mA\n"
                   f"Voltage compliance: {cfg['voltage_limit_v']:g} V\n"
                   f"Requested samples: {expected}\n"
                   f"Send analyzer axes: {'YES' if cfg['apply_axes'] else 'NO'}\n\n"
                   + '\n'.join(commands)
                   + ('\n\nCAUTION: I–V compliance is set to 100 mA (at the instrument limit),'
                      ' while the panel is rated for 300 mA. Compliance does not'
                      ' reliably limit externally generated current.'
                      ' Proceed only if the swept operating conditions were independently verified.'
                      if cfg['measurement']=='I–V Sweep' else
                      '\n\nReview the setup and verify the fixture, limits, and illumination before proceeding. Continue?'))
        if not messagebox.askyesno('Review instrument configuration',summary):return
        self.running=True
        self.run_btn.configure(state='disabled');self.preview_btn.configure(state='disabled')
        self.auto_analyzer_btn.configure(state='disabled')
        self.status.set('Configuring…')
        threading.Thread(target=self._acquire,args=(cfg,),daemon=True).start()

    def _acquire(self,cfg):
        try:
            cmds,records,expected=build_commands(cfg)
            self.events.put(('status','Configuring analyzer…'))
            with MeasurementSession(address=cfg['address'],allow_configuration=True) as analyzer:
                analyzer.configure_profile(cfg,fixture_verified=True)
                self.events.put(('status', f'Previous analyzer status before configuration: 0x{analyzer.last_preflight_status:02X}'))
                timeout=max(60,cfg['wait']+cfg['points']*cfg['interval']+30) if cfg['measurement']!='I–V Sweep' else 90
                self.events.put(('status','Measuring…'))
                analyzer.acquire_profile(fixture_verified=True,timeout=timeout)
                self.events.put(('status','Reading stored results…'))
                records=(records,) if isinstance(records,str) else records
                traces=[]
                for record in records:
                    best=None
                    for attempt in range(4):
                        try:
                            new=analyzer.get_data(record)
                            if best is None or len(new)>len(best):best=new
                            if len(new)>=expected:break
                        except HP4145AError:
                            if attempt==3 and best is None:raise
                        if attempt<3:time.sleep(1)
                    if best is None:raise HP4145AError(f'No values returned for {record}')
                    traces.append(best)
            stamp=datetime.now().strftime('%Y%m%d_%H%M%S_%f')
            out=Path('outputs');out.mkdir(exist_ok=True)
            prefix={'Current vs Time':'current_time','Voltage vs Time':'voltage_time','I–V Sweep':'iv_sweep'}[cfg['measurement']]
            csvpath=out/f'{prefix}_{stamp}.csv';pngpath=out/f'{prefix}_{stamp}.png'
            rows=min(len(t) for t in traces)
            if rows==0:raise HP4145AError('Empty trace')
            with csvpath.open('w',newline='') as f:
                writer=csv.writer(f)
                writer.writerow(['index']+[f'{name}_value_SI' for name in records]+[f'{name}_status' for name in records])
                for k in range(rows):
                    writer.writerow([k]+[t[k].value for t in traces]+[t[k].status for t in traces])
            self.events.put(('result',(cfg,records,traces,expected,csvpath,pngpath)))
        except Exception as exc:
            self.events.put(('error',str(exc)))
        finally:
            self.events.put(('finished',None))

    def _plot(self,data):
        cfg,names,traces,expected,csvpath,pngpath=data
        mode=cfg['measurement'];self.ax.clear()
        n=min(map(len,traces))
        if mode=='I–V Sweep':
            x=[r.value for r in traces[0][:n]]
            # The instrument's current is negative during solar generation.
            # Preserve raw signed values in CSV, and display generated current.
            y=[-r.value*1000 for r in traces[1][:n]]
            self.ax.plot(x,y,'o-',linewidth=2)
            self.ax.set(xlabel='Voltage (V)',ylabel='Generated Current (mA)',title='HP 4145A — Solar Cell I–V Curve')
        elif mode=='Voltage vs Time':
            x=[i*cfg['interval'] for i in range(n)]
            y=[r.value for r in traces[0][:n]]
            self.ax.plot(x,y,'o-',linewidth=2)
            self.ax.set(xlabel='Time (s)',ylabel='Voltage (V)',title='HP 4145A — Voltage vs Time')
        else:
            x=[i*cfg['interval'] for i in range(n)]
            y=[r.value*1000 for r in traces[0][:n]]
            if cfg['magnitude']:y=[abs(v) for v in y]
            self.ax.plot(x,y,'o-',linewidth=2)
            self.ax.set(xlabel='Time (s)',ylabel='Current (mA)',title='HP 4145A — Current vs Time')
        self.ax.grid(True,alpha=.35);self.ax.autoscale();self.fig.tight_layout()
        self.chart.draw();self.fig.savefig(pngpath,dpi=180)
        self._last_data=data
        self.status.set(f'Complete — {n} samples')
        self._log(f'Saved {n} sample pairs: {csvpath}; graph: {pngpath}')
        if any(len(t)!=expected for t in traces):
            self._log(f'WARNING: requested {expected}, received {[len(t) for t in traces]}. Trace saved as-is; check the analyzer NR/IN settings and acquisition status.')
        bad=[(name,i+1,r.status) for name,trace in zip(names,traces) for i,r in enumerate(trace) if not r.valid]
        if bad:self._log(f'WARNING: flagged readings: {bad[:25]}')

    def autoscale_plot(self):
        """Reset the Mac Matplotlib plot to fit currently displayed points."""
        if not self.ax.lines:
            self._log('No plot data yet: run a measurement first.')
            return
        self.ax.relim()
        self.ax.autoscale(enable=True,axis='both')
        self.ax.autoscale_view()
        self.fig.tight_layout()
        self.chart.draw_idle()
        self._log('Mac plot axes autoscaled. No GPIB command sent.')

    @staticmethod
    def _padded_limits(values):
        """Small margins around real stored data, never inventing samples."""
        import math
        good=[v for v in values if math.isfinite(v)]
        if not good:raise ValueError('No finite measured values to scale')
        lo,hi=min(good),max(good)
        pad=max((hi-lo)*0.06,abs(lo)*0.002,abs(hi)*0.002,1e-6)
        return lo-pad,hi+pad

    def autoscale_analyzer(self):
        """Fit the analyzer's physical screen using XN/XT and YA commands."""
        if self.running:
            self._log('Wait for the current acquisition to finish.')
            return
        if self._last_data is None:
            self._log('No completed data available to fit analyzer axes.')
            return
        cfg,names,traces,expected,csvpath,pngpath=self._last_data
        n=min(len(t) for t in traces)
        if cfg['measurement']=='I–V Sweep':
            # Fit only paired measurements with normal status flags.
            paired=[(v.value,i.value) for v,i in zip(traces[0][:n],traces[1][:n])
                    if v.valid and i.valid]
            x=[v for v,_ in paired]
            y=[i for _,i in paired] # Raw signed I1, in A.
            xlo,xhi=self._padded_limits(x)
            ylo,yhi=self._padded_limits(y)
            commands=[f"XN '{cfg['v1']}',1,{xlo:.8g},{xhi:.8g}",
                      f"YA '{cfg['i1']}',1,{ylo:.8g},{yhi:.8g}"]
        else:
            pairs=[(i*cfg['interval'],r.value) for i,r in enumerate(traces[0][:n])
                   if r.valid]
            x=[t for t,_ in pairs]
            y=[v for _,v in pairs]
            xlo,xhi=self._padded_limits(x)
            xlo=max(0.0,xlo)
            if not xlo < xhi:xhi=xlo+0.01
            ylo,yhi=self._padded_limits(y)
            commands=[f"XT {xlo:.8g},{xhi:.8g}",
                      f"YA '{names[0]}',1,{ylo:.8g},{yhi:.8g}"]
        self.running=True
        self.run_btn.configure(state='disabled')
        self.preview_btn.configure(state='disabled')
        self.auto_analyzer_btn.configure(state='disabled')
        self.status.set('Fitting analyzer axes…')
        threading.Thread(target=self._send_fit_commands,
                         args=(cfg['address'],commands),daemon=True).start()

    def _send_fit_commands(self,address,commands):
        """Only change display axes; never change SMU configuration/source."""
        try:
            with MeasurementSession(address=address) as analyzer:
                old_status=analyzer.serial_poll()
                self.events.put(('status', f'Previous analyzer status: 0x{old_status:02X}'))
                for command in commands:
                    analyzer._send(command)
                    status=analyzer.serial_poll()
                    if status & 0x0A:
                        raise HP4145AError(
                            f'Analyzer rejected display command {command!r} (0x{status:02X})')
            self.events.put(('status', 'Analyzer display fitted to stored data; sources unchanged.'))
            self.events.put(('status', 'Display commands: '+'; '.join(commands)))
        except Exception as exc:
            self.events.put(('error',str(exc)))
        finally:
            self.events.put(('finished',None))

    def _process_events(self):
        try:
            while True:
                kind,data=self.events.get_nowait()
                if kind=='status':self.status.set(data);self._log(data)
                elif kind=='error':
                    self.status.set('Error');self._log('ERROR: '+data)
                    messagebox.showerror('Measurement error',data)
                elif kind=='result':self._plot(data)
                elif kind=='finished':
                    self.running=False;self.run_btn.configure(state='normal')
                    self.preview_btn.configure(state='normal')
                    self.auto_analyzer_btn.configure(state='normal')
        except queue.Empty:pass
        self.after(100,self._process_events)

if __name__=='__main__':
    AnalyzerGUI().mainloop()
