using System;
using System.IO;
using System.Collections;

namespace TeamXBMC.TranslatorCore
{
	/// <summary>
	/// Represents an array of strings to be translated.
	/// </summary>
	public class TranslatorArray : ICollection
	{
		private ArrayList strings=new ArrayList();
		private Hashtable stringsMap=new Hashtable();
		private TranslatorArrayEnumerator enumerator=TranslatorArrayEnumerator.All;
		private double versionOriginal=0.0;

		#region Constructors

		public TranslatorArray()
		{
			//
			// TODO: Add constructor logic here
			//
		}

		#endregion

		#region ICollection Members

		public bool IsSynchronized
		{
			get{return false;}
		}

		public int Count
		{
			get{return strings.Count;}
		}

		public void CopyTo(Array array, int index)
		{
			strings.CopyTo(array, index);
		}

		public object SyncRoot
		{
			get{return this;}
		}

		#endregion

		#region IEnumerable Members

		public IEnumerator GetEnumerator()
		{
			switch (enumerator)
			{
				case TranslatorArrayEnumerator.All:
					return strings.GetEnumerator();
				case TranslatorArrayEnumerator.Translated:
					return new EnumeratorTranslated(strings.GetEnumerator());
				case TranslatorArrayEnumerator.Untranslated:
					return new EnumeratorUntranslated(strings.GetEnumerator());
				case TranslatorArrayEnumerator.Changed:
					return new EnumeratorChanged(strings.GetEnumerator());
				default:
					return strings.GetEnumerator();
			}
		}

		#endregion

		#region Array methods

		/// <summary>
		/// Adds a TranslatorItem to the array.
		/// </summary>
		private void Add(TranslatorItem item)
		{
			strings.Add(item);
			stringsMap.Add(item.StringOriginal.Id, item);
		}

		/// <summary>
		/// Sorts the array
		/// </summary>
		private void Sort()
		{
			strings.Sort();
		}

		/// <summary>
		/// Clears the array
		/// </summary>
		public void Clear()
		{
			strings.Clear();
			stringsMap.Clear();
			versionOriginal=0.0;
		}

		/// <summary>
		/// Returns a TranslatorItem by a certain id
		/// </summary>
		public bool GetItemById(long id, ref TranslatorItem item)
		{
			if (!stringsMap.Contains(id))
			{
				item=null;
				return false;
			}

			item=(TranslatorItem)stringsMap[id];

			return true;
		}

		#endregion

		#region Language file processing

		/// <summary>
		/// Loads the current language file and checks its string for its state
		/// </summary>
		public void Load()
		{
			if (Settings.Instance.LanguageFolder=="" || Settings.Instance.Language=="")
				return;

			if (!File.Exists(Settings.Instance.FilenameOriginal))
				throw new TranslatorException("File "+Settings.Instance.FilenameOriginal+" was not found. Please be sure your language folder is set properly.");

			if (!File.Exists(Settings.Instance.FilenameTranslated))
				throw new TranslatorException("File "+Settings.Instance.FilenameTranslated+" was not found. Please be sure your language folder is set properly.");

			StringArray stringsOriginal=new StringArray();
			stringsOriginal.Load(Settings.Instance.FilenameOriginal);

			versionOriginal=stringsOriginal.Version;

			StringArray stringsTranslated=new StringArray();
			stringsTranslated.Load(Settings.Instance.FilenameTranslated);

			try
			{
				DetermineTranslated(stringsTranslated, stringsOriginal);
				DetermineUntranslated(stringsTranslated, stringsOriginal);
				DetermineChanged(stringsTranslated, stringsOriginal);
				Sort();
			}
			catch(Exception e)
			{
				throw new TranslatorException("Error processing xml data", e);
			}
		}

		/// <summary>
		/// Save the current language file
		/// </summary>
		public void Save()
		{
			StringArray strings=new StringArray();

			enumerator=TranslatorArrayEnumerator.All;
			foreach (TranslatorItem item in this)
			{
				if (item.State==TranslationState.Translated || item.State==TranslationState.Changed)
					strings.Add(item.StringTranslated);
			}

			strings.Sort();

			ArrayList comments=new ArrayList();
			comments.Add("Language file translated with Team XBMC Translator");
			if (Settings.Instance.TranslatorName!="")
				comments.Add("Translator: "+Settings.Instance.TranslatorName);
			if (Settings.Instance.TranslatorEmail!="")
				comments.Add("Email: "+Settings.Instance.TranslatorEmail);

			DateTime time=DateTime.Now;
			comments.Add("Date of translation: "+time.GetDateTimeFormats(System.Globalization.CultureInfo.InvariantCulture)[0]);
			comments.Add("$Revision$");
			if (versionOriginal>0.0)
				comments.Add("Based on english strings version "+versionOriginal.ToString(System.Globalization.CultureInfo.InvariantCulture));

			strings.Save(Settings.Instance.FilenameTranslated, (string[])comments.ToArray(typeof(string)));
		}

		/// <summary>
		/// Creates all TranlatorItems with the state translated
		/// </summary>
		private void DetermineTranslated(StringArray stringsTranslated, StringArray stringsOriginal)
		{
			foreach (StringItem item in stringsOriginal)
			{
				StringItem itemTranslated=null;
				if (stringsTranslated.GetStringById(item.Id, ref itemTranslated))
				{
					Add(new TranslatorItem(itemTranslated, item, TranslationState.Translated));
				}
			}
		}

		/// <summary>
		/// Creates all TranlatorItems with the state untranslated
		/// </summary>
		private void DetermineUntranslated(StringArray stringsTranslated, StringArray stringsOriginal)
		{
			foreach (StringItem item in stringsOriginal.GetStringsNotIn(stringsTranslated))
			{
				StringItem itemUntranslated=new StringItem(item);
				itemUntranslated.Text="";
				Add(new TranslatorItem(itemUntranslated, item, TranslationState.Untranslated));
			}
		}

		/// <summary>
		/// Creates all TranlatorItems with the state changed.
		/// It also saves the master language history
		/// </summary>
		private void DetermineChanged(StringArray stringsTranslated, StringArray stringsOriginal)
		{
			string filenameSaved=System.Windows.Forms.Application.LocalUserAppDataPath.Substring(0, System.Windows.Forms.Application.LocalUserAppDataPath.LastIndexOf(@"\"))+@"\strings.xml";
			string filenamePrev=System.Windows.Forms.Application.LocalUserAppDataPath.Substring(0, System.Windows.Forms.Application.LocalUserAppDataPath.LastIndexOf(@"\"))+@"\stringsPrev.xml";

			// No history yet?
			if (!File.Exists(filenameSaved))
			{	// Copy our master language, to have a history
				File.Copy(Settings.Instance.FilenameOriginal, filenameSaved, true);
				return;
			}

			StringArray stringsSaved=new StringArray();

			stringsSaved.Load(filenameSaved);

			// Check the version of the saved master language
			if (stringsOriginal.Version>stringsSaved.Version)
			{	// saved is older then our current master language,
				// update history
				File.Copy(filenameSaved, filenamePrev, true);
				File.Copy(Settings.Instance.FilenameOriginal, filenameSaved, true);
			}
			else if (stringsOriginal.Version<stringsSaved.Version)
			{ // Oh Oh, current master is older then the saved one, thow exception
				throw new TranslatorException("File "+Settings.Instance.FilenameOriginal+" is older then the one last used for this translation.");
			}

			stringsSaved.Clear();

			if (File.Exists(filenamePrev))
			{	// we need at two file to do the diff
				StringArray stringsPrev=new StringArray();
				stringsPrev.Load(filenamePrev);

				// check for changed items
				foreach (long id in stringsPrev.GetStringsChangedIn(stringsOriginal))
				{
					TranslatorItem translatorItem=null;
					if (GetItemById(id, ref translatorItem))
					{
						if (translatorItem.StringTranslated.Text!="")
							translatorItem.State=TranslationState.Changed;
					}
				}
			}
		}

		#endregion

		#region Properties

		/// <summary>
		/// Gets/Sets the current enumerator. See
		/// TranslatorArrayEnumerator for more info.
		/// </summary>
		public TranslatorArrayEnumerator Enumerator
		{
			get { return enumerator; }
			set { enumerator=value; }
		}

		#endregion
	}
}
