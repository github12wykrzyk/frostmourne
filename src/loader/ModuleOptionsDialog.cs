using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace FrostmourneGui {
    // All controls are generated from module.json; no module-specific GUI changes.
    internal sealed class ModuleOptionsDialog : Form {
        readonly Module module;
        readonly Dictionary<string,Control> controls = new Dictionary<string,Control>();
        internal ModuleOptionsDialog(Module module) {
            this.module = module;
            Text = "FROSTMOURNE / " + module.Id + " / " + module.Manifest.version;
            Width = 520; Height = 540; MinimumSize = new Size(440,320);
            StartPosition = FormStartPosition.CenterParent;
            AutoScroll = true;
            FlowLayoutPanel panel = new FlowLayoutPanel {
                Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown, WrapContents = false,
                AutoScroll = true, Padding = new Padding(15)
            };
            Controls.Add(panel);
            Label help = new Label { Width=450,Height=50,
                Text="Konfiguracja modulu (zapisywana lokalnie). Eksperymentalne funkcje sa domyslnie wylaczone." };
            panel.Controls.Add(help);
            foreach (ModuleOption option in module.Manifest.options ?? new ModuleOption[0]) {
                Label label = new Label { Text=String.IsNullOrEmpty(option.label)?option.key:option.label,
                                          Width=430,Height=23 };
                panel.Controls.Add(label);
                string value;
                if (!module.Options.TryGetValue(option.key, out value)) value=option.default_value;
                Control input;
                if (option.type == "bool") {
                    input = new CheckBox { Checked=value=="true",Width=430,Height=28 };
                } else if (option.type == "int") {
                    int number = Int32.Parse(value);
                    input = new NumericUpDown { Minimum=option.min,Maximum=option.max,
                        Value=number,Width=180,Height=28 };
                } else if (option.type == "choice") {
                    ComboBox combo = new ComboBox { DropDownStyle=ComboBoxStyle.DropDownList,
                        Width=420,Height=28 };
                    combo.Items.AddRange(option.choices.Cast<object>().ToArray());
                    combo.SelectedItem=value; input=combo;
                } else throw new InvalidOperationException("Unsupported option type "+option.type);
                panel.Controls.Add(input); controls.Add(option.key,input);
            }
            Button apply=new Button { Text="Zapisz",Width=130,Height=32 };
            apply.Click += (s,e) => {
                try {
                    Dictionary<string,string> values = new Dictionary<string,string>();
                    foreach (ModuleOption option in module.Manifest.options ?? new ModuleOption[0]) {
                        Control input=controls[option.key];
                        string value=option.type=="bool" ? (((CheckBox)input).Checked?"true":"false")
                            : option.type=="int" ? ((int)((NumericUpDown)input).Value).ToString()
                            : Convert.ToString(((ComboBox)input).SelectedItem);
                        ModuleCatalog.ValidateOption(option,value);
                        values[option.key]=value;
                    }
                    module.Options.Clear();
                    foreach (KeyValuePair<string,string> pair in values) module.Options.Add(pair.Key,pair.Value);
                    DialogResult=DialogResult.OK; Close();
                } catch(Exception error) { MessageBox.Show(this,error.Message,"Opcja modulu",MessageBoxButtons.OK,MessageBoxIcon.Error); }
            };
            panel.Controls.Add(apply);
        }
    }
}
