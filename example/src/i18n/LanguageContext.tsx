import {
  createContext,
  useContext,
  useMemo,
  useState,
  type ReactNode,
} from 'react';
import { TEXTS, type Language, type Texts } from './texts';

/*
 * Which language the app is showing, and how a screen gets its sentences.
 *
 *   <LanguageProvider>          once, around the whole app
 *     ...
 *     const texts = useTexts();                     in any screen
 *     <Text>{texts.device.getReport}</Text>
 *
 * The app starts in the language of the phone. A different choice is kept
 * in memory only: closing the app forgets it.
 */

type LanguageChoice = Readonly<{
  language: Language;
  setLanguage: (language: Language) => void;
}>;

const LanguageContext = createContext<LanguageChoice>({
  language: 'en',
  setLanguage: () => {},
});

/**
 * Portuguese when the phone is set to Portuguese, English otherwise.
 *
 * On iOS this works only because the app declares both languages (the
 * folders en.lproj and pt.lproj in ios/LocationConfidenceExample). To an
 * app that declares none, iOS reports English on every phone.
 */
function languageOfThePhone(): Language {
  try {
    // For example "pt-BR" on a phone set to Portuguese of Brazil.
    const locale = Intl.DateTimeFormat().resolvedOptions().locale;
    const phoneIsInPortuguese = locale.toLowerCase().startsWith('pt');
    return phoneIsInPortuguese ? 'pt' : 'en';
  } catch {
    return 'en';
  }
}

export function LanguageProvider({ children }: { children: ReactNode }) {
  const [language, setLanguage] = useState<Language>(languageOfThePhone);
  const choice = useMemo(() => ({ language, setLanguage }), [language]);

  return (
    <LanguageContext.Provider value={choice}>
      {children}
    </LanguageContext.Provider>
  );
}

/** The language in use and the way to change it. For the language switch. */
export function useLanguage(): LanguageChoice {
  return useContext(LanguageContext);
}

/** Every sentence of the app in the language in use. */
export function useTexts(): Texts {
  const { language } = useContext(LanguageContext);
  return TEXTS[language];
}
