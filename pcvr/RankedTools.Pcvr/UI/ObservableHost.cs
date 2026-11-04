using System;
using System.ComponentModel;
using System.Reflection;
using BeatSaberMarkupLanguage.Attributes;

namespace RankedTools.Pcvr.UI;

internal abstract class ObservableHost : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;
    protected void Notify()
    {
        foreach (var property in GetType().GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
            if (property.GetCustomAttribute<UIValue>() != null)
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(property.Name));
    }
}
