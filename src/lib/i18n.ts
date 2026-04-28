export type Locale = "en";

type SiteCopy = {
  lang: string;
  brandNote: string;
  footer?: string;
  nav: Array<{ href: string; label: string }>;
};

export const siteCopy: Record<Locale, SiteCopy> = {
  en: {
    lang: "en",
    brandNote: "Projects, notes, and ongoing research.",
    nav: [
      { href: "/", label: "Home" },
      { href: "/projects", label: "Projects" },
      { href: "/notes", label: "Notes" },
      { href: "/about", label: "About" }
    ]
  }
};

export function formatDate(date: Date, locale: Locale = "en") {
  return date.toLocaleDateString(locale === "en" ? "en-CA" : "en-CA");
}
