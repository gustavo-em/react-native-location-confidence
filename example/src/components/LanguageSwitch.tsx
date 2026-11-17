import { Pressable, StyleSheet, Text, View } from 'react-native';
import { useLanguage, useTexts } from '../i18n/LanguageContext';
import { LANGUAGES } from '../i18n/texts';
import { colors, fonts } from '../theme';

/*
 * The choice of language at the top of the screen: "English | Português".
 *
 * With only two languages both stay visible, and one tap changes the whole
 * app. Each option is written in its own language, so a person finds theirs
 * whatever the app is showing at the moment.
 */
export function LanguageSwitch() {
  const { language, setLanguage } = useLanguage();
  const texts = useTexts();

  return (
    <View
      style={styles.options}
      accessibilityRole="radiogroup"
      accessibilityLabel={texts.language}
    >
      {LANGUAGES.map((candidate) => {
        const isPicked = candidate.id === language;
        return (
          <Pressable
            key={candidate.id}
            accessibilityRole="radio"
            accessibilityState={{ checked: isPicked }}
            hitSlop={TALLER_TOUCH_AREA}
            onPress={() => setLanguage(candidate.id)}
            style={[styles.option, isPicked && styles.optionPicked]}
          >
            <Text
              style={[styles.optionText, isPicked && styles.optionTextPicked]}
            >
              {candidate.name}
            </Text>
          </Pressable>
        );
      })}
    </View>
  );
}

// The options are short, so a finger gets some extra room above and below.
// Nothing is added on the sides: there the two options touch each other.
const TALLER_TOUCH_AREA = { top: 9, bottom: 9 };

const styles = StyleSheet.create({
  options: {
    flexDirection: 'row',
    marginLeft: 12,
    borderWidth: 1,
    borderColor: colors.ink,
    borderRadius: 4,
    overflow: 'hidden',
  },
  option: {
    paddingHorizontal: 9,
    paddingVertical: 5,
  },
  optionPicked: {
    backgroundColor: colors.ink,
  },
  optionText: {
    fontFamily: fonts.sign,
    fontWeight: '700',
    fontSize: 14,
    color: colors.ink,
  },
  optionTextPicked: {
    color: colors.white,
  },
});
